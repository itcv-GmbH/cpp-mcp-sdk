#!/usr/bin/env python3

import argparse
import asyncio
import json
import os
import subprocess
import sys
import traceback
from pathlib import Path
from typing import Any

import anyio

from mcp import ClientSession, types
from mcp.shared.message import SessionMessage


EXPECTED_CPP_SERVER_SAMPLING_PROMPT = "cpp-server-sampling-check"
EXPECTED_CPP_SERVER_ELICITATION_MESSAGE = "cpp-server-elicitation-check"
EXPECTED_REFERENCE_CLIENT_SAMPLING_RESPONSE = "reference-client-sampling-response"
EXPECTED_REFERENCE_CLIENT_ELICITATION_REASON = "reference-client-confirmed"

# Timeout for process termination before falling back to force kill
PROCESS_TERMINATION_TIMEOUT = 2.0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run reference Python client against C++ STDIO server fixture"
    )
    parser.add_argument(
        "--cpp-server",
        required=True,
        help="Path to C++ STDIO server fixture executable",
    )
    return parser.parse_args()


def content_to_texts(content_items: Any) -> list[str]:
    texts: list[str] = []
    for item in content_items:
        if hasattr(item, "text"):
            texts.append(str(item.text))
            continue
        if isinstance(item, dict) and "text" in item:
            texts.append(str(item["text"]))
            continue
        texts.append(str(item))
    return texts


def prompt_messages_to_texts(prompt_result: Any) -> list[str]:
    texts: list[str] = []
    for message in getattr(prompt_result, "messages", []):
        content = getattr(message, "content", None)
        text_value = getattr(content, "text", None)
        if text_value is not None:
            texts.append(str(text_value))
            continue
        if isinstance(content, dict) and "text" in content:
            texts.append(str(content["text"]))
            continue
        texts.append(str(content))
    return texts


def extract_sampling_prompt_text(params: Any) -> str:
    messages = getattr(params, "messages", [])
    if not messages:
        return ""

    content = getattr(messages[-1], "content", None)
    if content is None:
        return ""

    text_value = getattr(content, "text", None)
    if text_value is not None:
        return str(text_value)

    if isinstance(content, dict) and "text" in content:
        return str(content["text"])

    return str(content)


async def run_stdio_client_flow(
    read_stream: "anyio.MemoryObjectReceiveStream[SessionMessage | Exception]",
    write_stream: "anyio.MemoryObjectSendStream[SessionMessage]",
) -> None:
    """Run the authenticated flow over stdio transport using provided streams."""
    sampling_request_observed = asyncio.Event()
    elicitation_request_observed = asyncio.Event()

    async def handle_sampling(
        _context: Any, params: types.CreateMessageRequestParams
    ) -> types.CreateMessageResult:
        prompt_text = extract_sampling_prompt_text(params)
        if EXPECTED_CPP_SERVER_SAMPLING_PROMPT not in prompt_text:
            raise AssertionError(
                f"Unexpected sampling prompt from C++ server: {prompt_text!r}"
            )

        sampling_request_observed.set()
        return types.CreateMessageResult(
            role="assistant",
            model="reference-client-model",
            content=types.TextContent(
                type="text",
                text=EXPECTED_REFERENCE_CLIENT_SAMPLING_RESPONSE,
            ),
            stop_reason="endTurn",
        )

    async def handle_elicitation(
        _context: Any, params: types.ElicitRequestParams
    ) -> types.ElicitResult:
        if params.message != EXPECTED_CPP_SERVER_ELICITATION_MESSAGE:
            raise AssertionError(
                f"Unexpected elicitation message from C++ server: {params.message!r}"
            )

        elicitation_request_observed.set()
        return types.ElicitResult(
            action="accept",
            content={
                "approved": True,
                "reason": EXPECTED_REFERENCE_CLIENT_ELICITATION_REASON,
            },
        )

    async with ClientSession(
        read_stream,
        write_stream,
        sampling_callback=handle_sampling,
        elicitation_callback=handle_elicitation,
    ) as session:
        await session.initialize()

        # Verify tools
        tools_result = await session.list_tools()
        tool_names = [tool.name for tool in tools_result.tools]
        if "cpp_echo" not in tool_names:
            raise AssertionError(f"cpp_echo not found in tools list: {tool_names}")

        # Call the tool and verify response
        call_result = await session.call_tool(
            "cpp_echo", {"text": "from-reference-client"}
        )
        if getattr(call_result, "isError", False):
            raise AssertionError(f"cpp_echo call returned isError=true: {call_result}")

        tool_texts = content_to_texts(getattr(call_result, "content", []))
        if not any("from-reference-client" in text for text in tool_texts):
            raise AssertionError(
                f"cpp_echo response did not include expected text. content={tool_texts}"
            )

        # Verify resources
        resources_result = await session.list_resources()
        resource_uris = [str(resource.uri) for resource in resources_result.resources]
        if "resource://cpp-stdio-server/info" not in resource_uris:
            raise AssertionError(f"Expected resource URI not found: {resource_uris}")

        read_result = await session.read_resource("resource://cpp-stdio-server/info")
        resource_texts = content_to_texts(getattr(read_result, "contents", []))
        if not any("cpp stdio integration resource" in text for text in resource_texts):
            raise AssertionError(
                f"Resource read result missing expected marker. contents={resource_texts}"
            )

        # Verify prompts
        prompts_result = await session.list_prompts()
        prompt_names = [prompt.name for prompt in prompts_result.prompts]
        if "cpp_stdio_server_prompt" not in prompt_names:
            raise AssertionError(
                f"cpp_stdio_server_prompt not found in prompt list: {prompt_names}"
            )

        prompt_result = await session.get_prompt(
            "cpp_stdio_server_prompt", {"topic": "interop"}
        )
        prompt_texts = prompt_messages_to_texts(prompt_result)
        if not any("interop" in text for text in prompt_texts):
            raise AssertionError(
                f"Prompt response did not include expected topic. messages={prompt_texts}"
            )

        # Wait for server-initiated requests to complete
        await asyncio.wait_for(sampling_request_observed.wait(), timeout=30.0)
        await asyncio.wait_for(elicitation_request_observed.wait(), timeout=30.0)

        # Give the server time to process our responses to its outbound requests
        # before we close the connection. This is critical for the C++ fixture's
        # outbound assertion checks to pass.
        await asyncio.sleep(2.0)


async def stdout_reader_task(
    stdout_fd: int,
    read_stream_writer: "anyio.MemoryObjectSendStream[SessionMessage | Exception]",
) -> None:
    """Task that reads from process stdout using async I/O and sends messages to the read stream."""
    buffer = ""
    try:
        async with read_stream_writer:
            while True:
                # Wait for data to be available asynchronously
                try:
                    await anyio.wait_readable(stdout_fd)
                except anyio.BrokenResourceError:
                    # EOF or pipe closed
                    break

                # Read data from stdout (non-blocking read via thread pool)
                data = await anyio.to_thread.run_sync(os.read, stdout_fd, 8192)
                if not data:
                    # EOF
                    break

                # Process as text - handle line buffering properly
                text = data.decode("utf-8", errors="replace")
                lines = (buffer + text).split("\n")
                # Last part might be incomplete, keep in buffer
                buffer = lines.pop() if lines else ""

                for line in lines:
                    if line.strip():
                        try:
                            message = types.JSONRPCMessage.model_validate_json(line)
                            session_message = SessionMessage(message)
                            await read_stream_writer.send(session_message)
                        except Exception as exc:
                            import logging

                            logging.getLogger(__name__).exception(
                                f"Failed to parse JSONRPC message: {line[:100]}"
                            )
                            await read_stream_writer.send(exc)

                # Handle any remaining buffer at EOF
                if not data:
                    if buffer.strip():
                        try:
                            message = types.JSONRPCMessage.model_validate_json(buffer)
                            session_message = SessionMessage(message)
                            await read_stream_writer.send(session_message)
                        except Exception:
                            pass
    except anyio.ClosedResourceError:
        pass


async def stdin_writer_task(
    stdin_fd: int,
    write_stream_reader: "anyio.MemoryObjectReceiveStream[SessionMessage]",
) -> None:
    """Task that writes messages from the write stream to process stdin using async I/O."""
    try:
        async with write_stream_reader:
            async for session_message in write_stream_reader:
                json_str = session_message.message.model_dump_json(
                    by_alias=True, exclude_none=True
                )
                data = (json_str + "\n").encode("utf-8", errors="replace")
                await anyio.to_thread.run_sync(os.write, stdin_fd, data)
    except anyio.ClosedResourceError:
        pass


async def run() -> int:
    args = parse_args()
    server_executable = str(Path(args.cpp_server).resolve())

    # Spawn the process manually using subprocess.Popen
    process = subprocess.Popen(
        [server_executable],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    # Get file descriptors for stdin/stdout for async I/O
    stdin_fd = process.stdin.fileno()
    stdout_fd = process.stdout.fileno()

    # Create memory object streams (same as stdio_client does internally)
    read_stream_writer: "anyio.MemoryObjectSendStream[SessionMessage | Exception]"
    read_stream: "anyio.MemoryObjectReceiveStream[SessionMessage | Exception]"
    write_stream: "anyio.MemoryObjectSendStream[SessionMessage]"
    write_stream_reader: "anyio.MemoryObjectReceiveStream[SessionMessage]"

    read_stream_writer, read_stream = anyio.create_memory_object_stream(0)
    write_stream, write_stream_reader = anyio.create_memory_object_stream(0)

    client_error = None

    # Create background tasks for I/O
    reader_task = asyncio.create_task(stdout_reader_task(stdout_fd, read_stream_writer))
    writer_task = asyncio.create_task(stdin_writer_task(stdin_fd, write_stream_reader))

    # Run the client flow
    try:
        await run_stdio_client_flow(read_stream, write_stream)
    except Exception as e:
        client_error = e
    finally:
        # Close the streams to signal EOF to the I/O tasks
        await read_stream.aclose()
        await write_stream.aclose()
        await read_stream_writer.aclose()
        await write_stream_reader.aclose()

        # Wait for I/O tasks to finish with timeout
        try:
            await asyncio.wait_for(reader_task, timeout=2.0)
        except asyncio.TimeoutError:
            reader_task.cancel()
        try:
            await asyncio.wait_for(writer_task, timeout=2.0)
        except asyncio.TimeoutError:
            writer_task.cancel()

    if client_error is not None:
        rendered = "".join(traceback.format_exception(client_error))
        print(rendered, file=sys.stderr)
        if process.stderr:
            stderr_output = process.stderr.read()
            if stderr_output:
                print(f"Server stderr:\n{stderr_output}", file=sys.stderr)
        # Force kill process on error
        if process.stdin and not process.stdin.closed:
            try:
                process.stdin.close()
            except Exception:
                pass
        process.kill()
        process.wait()
        return 1

    # MCP spec: stdio shutdown sequence
    # 1. Close input stream to server
    if process.stdin and not process.stdin.closed:
        try:
            process.stdin.close()
        except Exception:
            pass

    # 2. Wait for server to exit
    try:
        await asyncio.wait_for(
            asyncio.get_event_loop().run_in_executor(None, process.wait),
            timeout=PROCESS_TERMINATION_TIMEOUT,
        )
    except asyncio.TimeoutError:
        process.terminate()
        try:
            await asyncio.wait_for(
                asyncio.get_event_loop().run_in_executor(None, process.wait),
                timeout=PROCESS_TERMINATION_TIMEOUT,
            )
        except asyncio.TimeoutError:
            process.kill()
            process.wait()
    except ProcessLookupError:
        pass

    # Check process exit code
    if process.returncode != 0:
        stderr_output = ""
        if process.stderr:
            stderr_output = process.stderr.read()
        print(
            f"C++ server fixture exited with non-zero code {process.returncode}",
            file=sys.stderr,
        )
        if stderr_output:
            print(f"Server stderr:\n{stderr_output}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    result = asyncio.run(run())
    if result == 0:
        print(
            json.dumps(
                {"result": "ok", "script": "reference_client_to_cpp_stdio_server"}
            )
        )
    raise SystemExit(result)
