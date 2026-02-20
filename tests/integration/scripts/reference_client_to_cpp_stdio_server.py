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
import anyio.abc
import anyio.streams.file
import anyio.streams.text

from mcp import ClientSession, types
from mcp.client.stdio import StdioServerParameters, stdio_client
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


class PopenProcessWrapper(anyio.abc.Process):
    """
    Wrapper around subprocess.Popen that implements the anyio.abc.Process interface.
    This allows us to use subprocess.Popen for process spawning while using the official
    mcp.client.stdio.stdio_client transport API.
    """

    def __init__(self, popen: subprocess.Popen):
        self._popen = popen
        # Create anyio stream wrappers for stdin/stdout/stderr
        self._stdin_stream: anyio.streams.file.FileWriteStream | None = None
        self._stdout_stream: anyio.streams.file.FileReadStream | None = None
        self._stderr_stream: anyio.streams.file.FileReadStream | None = None

    def _get_stdin_stream(self) -> anyio.streams.file.FileWriteStream:
        if self._stdin_stream is None:
            # Create unbuffered binary stream from stdin pipe
            self._stdin_stream = anyio.streams.file.FileWriteStream(self._popen.stdin)
        return self._stdin_stream

    def _get_stdout_stream(self) -> anyio.streams.file.FileReadStream:
        if self._stdout_stream is None:
            # Create unbuffered binary stream from stdout pipe
            self._stdout_stream = anyio.streams.file.FileReadStream(self._popen.stdout)
        return self._stdout_stream

    def _get_stderr_stream(self) -> anyio.streams.file.FileReadStream | None:
        if self._stderr_stream is None and self._popen.stderr is not None:
            # Create unbuffered binary stream from stderr pipe
            self._stderr_stream = anyio.streams.file.FileReadStream(self._popen.stderr)
        return self._stderr_stream

    @property
    def pid(self) -> int:
        return self._popen.pid

    @property
    def returncode(self) -> int | None:
        return self._popen.returncode

    @property
    def stdin(self) -> anyio.streams.file.FileWriteStream:
        return self._get_stdin_stream()

    @property
    def stdout(self) -> anyio.streams.file.FileReadStream:
        return self._get_stdout_stream()

    @property
    def stderr(self) -> anyio.streams.file.FileReadStream | None:
        return self._get_stderr_stream()

    async def wait(self) -> int:
        """Wait for the process to exit."""
        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(None, self._popen.wait)

    async def kill(self) -> None:
        """Kill the process."""
        loop = asyncio.get_event_loop()
        await loop.run_in_executor(None, self._popen.kill)

    async def terminate(self) -> None:
        """Terminate the process."""
        loop = asyncio.get_event_loop()
        await loop.run_in_executor(None, self._popen.terminate)

    async def send_signal(self, sig: int) -> None:
        """Send a signal to the process."""
        loop = asyncio.get_event_loop()
        await loop.run_in_executor(None, self._popen.send_signal, sig)

    async def aclose(self) -> None:
        """Close the process and its streams."""
        if self._stdin_stream is not None:
            await self._stdin_stream.aclose()
        if self._stdout_stream is not None:
            await self._stdout_stream.aclose()
        if self._stderr_stream is not None:
            await self._stderr_stream.aclose()
        # Note: we don't wait() here because the process management
        # (including wait for termination) is handled by stdio_client


class PopenProcessWrapperFactory:
    """
    Factory that creates PopenProcessWrapper instances.
    This is used to inject our pre-spawned process into the stdio_client.
    """

    def __init__(self, popen: subprocess.Popen):
        self._popen = popen

    async def __call__(
        self,
        command: str,
        args: list[str],
        env: dict[str, str] | None = None,
        errlog: Any = None,
        cwd: Path | str | None = None,
    ) -> PopenProcessWrapper:
        """Create a PopenProcessWrapper around our pre-spawned Popen."""
        return PopenProcessWrapper(self._popen)


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


async def run() -> int:
    args = parse_args()
    server_executable = str(Path(args.cpp_server).resolve())

    # Spawn the process manually using subprocess.Popen
    # Using bufsize=0 for unbuffered I/O and start_new_session for proper process group
    process = subprocess.Popen(
        [server_executable],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0,
        start_new_session=True,
    )

    # Create the factory that will wrap our Popen
    factory = PopenProcessWrapperFactory(process)

    # Monkeypatch _create_platform_compatible_process to use our factory
    # This allows stdio_client to use our pre-spawned process while still
    # using the official stdio transport API
    import mcp.client.stdio as stdio_module

    original_create_process = stdio_module._create_platform_compatible_process
    stdio_module._create_platform_compatible_process = factory

    client_error = None

    try:
        # Use the official stdio_client API to get the streams
        # The process is already spawned; stdio_client will manage I/O through our wrapper
        async with stdio_client(
            StdioServerParameters(
                command=server_executable,
                args=[],
            )
        ) as (read_stream, write_stream):
            # Run the client flow using the streams from stdio_client
            await run_stdio_client_flow(read_stream, write_stream)

    except Exception as e:
        client_error = e
    finally:
        # Restore the original function
        stdio_module._create_platform_compatible_process = original_create_process

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
    # Note: The stdio_client context manager already handles:
    # 1. Closing stdin to signal EOF to the server
    # 2. Waiting for server to exit
    # 3. SIGTERM -> SIGKILL escalation if needed
    # However, we need to explicitly check the exit code since we're managing the process

    # Check process exit code
    if process.returncode is None:
        # Process still running - wait for it
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
    elif process.returncode != 0:
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
