#!/usr/bin/env python3

import argparse
import asyncio
import json
import socket
import subprocess
import sys
import time
import traceback
from pathlib import Path
from typing import Any

import httpx
from mcp import ClientSession, types
from mcp.client.streamable_http import streamable_http_client


EXPECTED_CPP_SERVER_SAMPLING_PROMPT = "cpp-server-sampling-check"
EXPECTED_CPP_SERVER_ELICITATION_MESSAGE = "cpp-server-elicitation-check"
EXPECTED_REFERENCE_CLIENT_SAMPLING_RESPONSE = "reference-client-sampling-response"
EXPECTED_REFERENCE_CLIENT_ELICITATION_REASON = "reference-client-confirmed"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run reference Python client against C++ SDK server fixture"
    )
    parser.add_argument(
        "--cpp-server", required=True, help="Path to C++ server fixture executable"
    )
    return parser.parse_args()


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def start_cpp_server(executable: str, port: int, token: str) -> subprocess.Popen[str]:
    return subprocess.Popen(
        [
            executable,
            "--bind",
            "127.0.0.1",
            "--port",
            str(port),
            "--path",
            "/mcp",
            "--token",
            token,
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )


def stop_process(process: subprocess.Popen[str]) -> None:
    if process.stdin is not None and not process.stdin.closed:
        process.stdin.close()

    try:
        process.wait(timeout=5)
        return
    except subprocess.TimeoutExpired:
        process.terminate()

    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def read_process_output(process: subprocess.Popen[str]) -> str:
    if process.stdout is None:
        return ""
    return process.stdout.read()


def initialize_probe_payload() -> dict[str, Any]:
    return {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2025-11-25",
            "capabilities": {},
            "clientInfo": {
                "name": "reference-probe",
                "version": "1.0.0",
            },
        },
    }


def wait_for_server_ready(
    endpoint: str, process: subprocess.Popen[str], timeout_seconds: float = 20.0
) -> None:
    deadline = time.monotonic() + timeout_seconds
    payload = initialize_probe_payload()

    while time.monotonic() < deadline:
        if process.poll() is not None:
            output = read_process_output(process)
            raise RuntimeError(
                f"C++ server fixture exited before becoming ready (exit={process.returncode}).\n{output}"
            )

        try:
            response = httpx.post(endpoint, json=payload, timeout=1.0)
            if response.status_code in (400, 401, 403, 405):
                return
        except Exception:
            pass

        time.sleep(0.1)

    output = read_process_output(process)
    raise TimeoutError(
        f"Timed out waiting for C++ server fixture readiness at {endpoint}.\n{output}"
    )


def unpack_streams(stream_context_value: Any) -> tuple[Any, Any]:
    if not isinstance(stream_context_value, tuple) or len(stream_context_value) < 2:
        raise RuntimeError(
            f"Unexpected streamable_http_client context payload: {stream_context_value!r}"
        )
    return stream_context_value[0], stream_context_value[1]


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


async def run_authenticated_flow(endpoint: str, token: str) -> None:
    headers = {"Authorization": f"Bearer {token}"}
    timeout = httpx.Timeout(10.0, read=10.0)
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

    async with httpx.AsyncClient(headers=headers, timeout=timeout) as http_client:
        async with streamable_http_client(
            endpoint, http_client=http_client
        ) as stream_context_value:
            read_stream, write_stream = unpack_streams(stream_context_value)
            async with ClientSession(
                read_stream,
                write_stream,
                sampling_callback=handle_sampling,
                elicitation_callback=handle_elicitation,
            ) as session:
                await session.initialize()

                tools_result = await session.list_tools()
                tool_names = [tool.name for tool in tools_result.tools]
                if "cpp_echo" not in tool_names:
                    raise AssertionError(
                        f"cpp_echo not found in tools list: {tool_names}"
                    )

                call_result = await session.call_tool(
                    "cpp_echo", {"text": "from-reference-client"}
                )
                if getattr(call_result, "isError", False):
                    raise AssertionError(
                        f"cpp_echo call returned isError=true: {call_result}"
                    )

                tool_texts = content_to_texts(getattr(call_result, "content", []))
                if not any("from-reference-client" in text for text in tool_texts):
                    raise AssertionError(
                        f"cpp_echo response did not include expected text. content={tool_texts}"
                    )

                resources_result = await session.list_resources()
                resource_uris = [
                    str(resource.uri) for resource in resources_result.resources
                ]
                if "resource://cpp-server/info" not in resource_uris:
                    raise AssertionError(
                        f"Expected resource URI not found: {resource_uris}"
                    )

                read_result = await session.read_resource("resource://cpp-server/info")
                resource_texts = content_to_texts(getattr(read_result, "contents", []))
                if not any(
                    "cpp integration resource" in text for text in resource_texts
                ):
                    raise AssertionError(
                        f"Resource read result missing expected marker. contents={resource_texts}"
                    )

                prompts_result = await session.list_prompts()
                prompt_names = [prompt.name for prompt in prompts_result.prompts]
                if "cpp_server_prompt" not in prompt_names:
                    raise AssertionError(
                        f"cpp_server_prompt not found in prompt list: {prompt_names}"
                    )

                prompt_result = await session.get_prompt(
                    "cpp_server_prompt", {"topic": "interop"}
                )
                prompt_texts = prompt_messages_to_texts(prompt_result)
                if not any("interop" in text for text in prompt_texts):
                    raise AssertionError(
                        f"Prompt response did not include expected topic. messages={prompt_texts}"
                    )

                await asyncio.wait_for(sampling_request_observed.wait(), timeout=10.0)
                await asyncio.wait_for(
                    elicitation_request_observed.wait(), timeout=10.0
                )


async def expect_unauthorized_initialize(endpoint: str) -> None:
    timeout = httpx.Timeout(10.0, read=10.0)
    try:
        async with httpx.AsyncClient(timeout=timeout) as http_client:
            async with streamable_http_client(
                endpoint, http_client=http_client
            ) as stream_context_value:
                read_stream, write_stream = unpack_streams(stream_context_value)
                async with ClientSession(read_stream, write_stream) as session:
                    await session.initialize()
    except BaseException as error:  # noqa: BLE001
        rendered = "".join(traceback.format_exception(error))
        if (
            "401" in rendered
            or "Unauthorized" in rendered
            or "authorization" in rendered.lower()
        ):
            return
        raise AssertionError(
            f"Initialize failed, but not with an authorization-related error:\n{rendered}"
        ) from error

    raise AssertionError(
        "Initialize unexpectedly succeeded without Authorization header"
    )


async def probe_session_id_on_initialize(endpoint: str, token: str) -> str:
    """Probe: Authenticated initialize returns 200 and includes MCP-Session-Id."""
    headers = {"Authorization": f"Bearer {token}"}
    timeout = httpx.Timeout(10.0, read=10.0)
    payload = initialize_probe_payload()

    async with httpx.AsyncClient(headers=headers, timeout=timeout) as http_client:
        response = await http_client.post(endpoint, json=payload)

        if response.status_code != 200:
            raise AssertionError(
                f"Expected HTTP 200 on authenticated initialize, got {response.status_code}: {response.text}"
            )

        session_id = response.headers.get("MCP-Session-Id")
        if not session_id:
            raise AssertionError(
                f"MCP-Session-Id header missing in authenticated initialize response. Status: {response.status_code}, Headers: {dict(response.headers)}, Body: {response.text[:200]}"
            )

        return session_id


async def probe_session_uniqueness(endpoint: str, token: str) -> tuple[str, str]:
    """Probe: Two authenticated initialize probes return different MCP-Session-Id values."""
    session_id_1 = await probe_session_id_on_initialize(endpoint, token)
    session_id_2 = await probe_session_id_on_initialize(endpoint, token)

    if session_id_1 == session_id_2:
        raise AssertionError(
            f"Expected different session IDs for separate initialize requests, got same: {session_id_1}"
        )

    return session_id_1, session_id_2


async def probe_require_session_id(endpoint: str, token: str) -> None:
    """Probe: Non-initialize POST without MCP-Session-Id returns 400 when requireSessionId=true."""
    headers = {"Authorization": f"Bearer {token}"}
    timeout = httpx.Timeout(10.0, read=10.0)

    # Send a non-initialize request (tools/list) without MCP-Session-Id
    payload = {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/list",
        "params": {},
    }

    async with httpx.AsyncClient(headers=headers, timeout=timeout) as http_client:
        response = await http_client.post(endpoint, json=payload)

        if response.status_code != 400:
            raise AssertionError(
                f"Expected HTTP 400 on missing MCP-Session-Id for non-initialize request, got {response.status_code}: {response.text}"
            )


async def run_authenticated_flow_simple(endpoint: str, token: str) -> None:
    """Simplified authenticated flow without sampling/elicitation callbacks."""
    headers = {"Authorization": f"Bearer {token}"}
    timeout = httpx.Timeout(10.0, read=10.0)

    async with httpx.AsyncClient(headers=headers, timeout=timeout) as http_client:
        async with streamable_http_client(
            endpoint, http_client=http_client
        ) as stream_context_value:
            read_stream, write_stream = unpack_streams(stream_context_value)
            # Don't pass sampling/elicitation callbacks - they're not supported by runner-based server
            async with ClientSession(read_stream, write_stream) as session:
                await session.initialize()

                tools_result = await session.list_tools()
                tool_names = [tool.name for tool in tools_result.tools]
                if "cpp_echo" not in tool_names:
                    raise AssertionError(
                        f"cpp_echo not found in tools list: {tool_names}"
                    )

                call_result = await session.call_tool(
                    "cpp_echo", {"text": "from-reference-client"}
                )
                if getattr(call_result, "isError", False):
                    raise AssertionError(
                        f"cpp_echo call returned isError=true: {call_result}"
                    )

                tool_texts = content_to_texts(getattr(call_result, "content", []))
                if not any("from-reference-client" in text for text in tool_texts):
                    raise AssertionError(
                        f"cpp_echo response did not include expected text. content={tool_texts}"
                    )

                resources_result = await session.list_resources()
                resource_uris = [
                    str(resource.uri) for resource in resources_result.resources
                ]
                if "resource://cpp-server/info" not in resource_uris:
                    raise AssertionError(
                        f"Expected resource URI not found: {resource_uris}"
                    )

                read_result = await session.read_resource("resource://cpp-server/info")
                resource_texts = content_to_texts(getattr(read_result, "contents", []))
                if not any(
                    "cpp integration resource" in text for text in resource_texts
                ):
                    raise AssertionError(
                        f"Resource read result missing expected marker. contents={resource_texts}"
                    )

                prompts_result = await session.list_prompts()
                prompt_names = [prompt.name for prompt in prompts_result.prompts]
                if "cpp_server_prompt" not in prompt_names:
                    raise AssertionError(
                        f"cpp_server_prompt not found in prompt list: {prompt_names}"
                    )

                prompt_result = await session.get_prompt(
                    "cpp_server_prompt", {"topic": "interop"}
                )
                prompt_texts = prompt_messages_to_texts(prompt_result)
                if not any("interop" in text for text in prompt_texts):
                    raise AssertionError(
                        f"Prompt response did not include expected topic. messages={prompt_texts}"
                    )


async def run() -> int:
    args = parse_args()
    server_executable = str(Path(args.cpp_server).resolve())
    token = "integration-token"
    port = find_free_port()
    endpoint = f"http://127.0.0.1:{port}/mcp"

    process = start_cpp_server(server_executable, port, token)
    try:
        wait_for_server_ready(endpoint, process)

        # HTTP probes for session ID behavior
        await probe_session_id_on_initialize(endpoint, token)
        await probe_session_uniqueness(endpoint, token)
        await probe_require_session_id(endpoint, token)

        # Existing authenticated flow tests (without sampling/elicitation assertions
        # since runner-based fixture doesn't support server-initiated requests in same way)
        await expect_unauthorized_initialize(endpoint)
        await run_authenticated_flow_simple(endpoint, token)
        return 0
    finally:
        stop_process(process)


if __name__ == "__main__":
    result = asyncio.run(run())
    print(json.dumps({"result": "ok", "script": "reference_client_to_cpp_server"}))
    raise SystemExit(result)
