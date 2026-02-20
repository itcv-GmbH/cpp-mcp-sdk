#!/usr/bin/env python3

import argparse
import asyncio
import json
import subprocess
import sys
import traceback
from pathlib import Path
from typing import Any

from mcp import ClientSession, types
from mcp.client.stdio import stdio_client, StdioServerParameters


EXPECTED_CPP_SERVER_SAMPLING_PROMPT = "cpp-server-sampling-check"
EXPECTED_CPP_SERVER_ELICITATION_MESSAGE = "cpp-server-elicitation-check"
EXPECTED_REFERENCE_CLIENT_SAMPLING_RESPONSE = "reference-client-sampling-response"
EXPECTED_REFERENCE_CLIENT_ELICITATION_REASON = "reference-client-confirmed"


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


async def run_stdio_client_flow(cpp_server_executable: str) -> None:
    """Run the authenticated flow over stdio transport."""
    sampling_request_observed = asyncio.Event()
    elicitation_request_observed = asyncio.Event()

    server_params = StdioServerParameters(
        command=cpp_server_executable,
        args=[],
    )

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

    async with stdio_client(server_params) as (read_stream, write_stream):
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
                raise AssertionError(
                    f"cpp_echo call returned isError=true: {call_result}"
                )

            tool_texts = content_to_texts(getattr(call_result, "content", []))
            if not any("from-reference-client" in text for text in tool_texts):
                raise AssertionError(
                    f"cpp_echo response did not include expected text. content={tool_texts}"
                )

            # Verify resources
            resources_result = await session.list_resources()
            resource_uris = [
                str(resource.uri) for resource in resources_result.resources
            ]
            if "resource://cpp-stdio-server/info" not in resource_uris:
                raise AssertionError(
                    f"Expected resource URI not found: {resource_uris}"
                )

            read_result = await session.read_resource(
                "resource://cpp-stdio-server/info"
            )
            resource_texts = content_to_texts(getattr(read_result, "contents", []))
            if not any(
                "cpp stdio integration resource" in text for text in resource_texts
            ):
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


async def run() -> int:
    args = parse_args()
    server_executable = str(Path(args.cpp_server).resolve())

    try:
        await run_stdio_client_flow(server_executable)
        return 0
    except Exception as error:
        rendered = "".join(traceback.format_exception(error))
        print(rendered, file=sys.stderr)
        return 1


if __name__ == "__main__":
    result = asyncio.run(run())
    if result == 0:
        print(
            json.dumps(
                {"result": "ok", "script": "reference_client_to_cpp_stdio_server"}
            )
        )
    raise SystemExit(result)
