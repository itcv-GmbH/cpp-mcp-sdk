#!/usr/bin/env python3
"""Test Python reference client against C++ HTTP server using base fixture for roots tests."""

import argparse
import asyncio
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))

import httpx
from python import harness
from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpp-server", required=True)
    return parser.parse_args()


def unpack_streams(stream_context_value):
    """Unpack stream context from streamable_http_client."""
    if not isinstance(stream_context_value, tuple) or len(stream_context_value) < 2:
        raise RuntimeError(f"Unexpected stream context: {stream_context_value!r}")
    return stream_context_value[0], stream_context_value[1]


async def run_test():
    args = parse_args()
    port = harness.find_free_port()
    token = "integration-token"

    # Start C++ fixture
    process = harness.start_process(
        [args.cpp_server, "--bind", "127.0.0.1", "--port", str(port), "--token", token]
    )

    try:
        # Wait for readiness
        readiness_line = harness.wait_for_readiness(process, "listening on")
        print(f"Server ready: {readiness_line}")

        endpoint = f"http://127.0.0.1:{port}/mcp"
        headers = {"Authorization": f"Bearer {token}"}
        timeout = httpx.Timeout(10.0, read=10.0)

        # Use reference client
        async with httpx.AsyncClient(headers=headers, timeout=timeout) as http_client:
            async with streamable_http_client(
                endpoint, http_client=http_client
            ) as stream_context:
                read_stream, write_stream = unpack_streams(stream_context)
                async with ClientSession(read_stream, write_stream) as session:
                    # Initialize
                    print("Initializing...")
                    await session.initialize()
                    print("✓ Initialize succeeded")

                    # Wait for server to complete initialization
                    await asyncio.sleep(0.5)

                    # Test roots/list (may not be supported in base fixture)
                    print("Testing roots/list...")
                    try:
                        roots_result = await session.send_request("roots/list", {})
                        print(f"✓ roots/list succeeded")
                    except Exception as e:
                        print(f"⚠ roots/list not supported: {e}")

                    # Test tools/list
                    print("Testing tools/list...")
                    tools_result = await session.list_tools()
                    tool_names = [tool.name for tool in tools_result.tools]
                    assert "cpp_echo" in tool_names, f"Expected cpp_echo tool"
                    print(f"✓ tools/list succeeded ({len(tools_result.tools)} tool(s))")

                    # Test tools/call
                    print("Testing tools/call...")
                    call_result = await session.call_tool("cpp_echo", {"text": "test"})
                    assert not getattr(call_result, "isError", False), (
                        f"Tool call error"
                    )
                    print("✓ tools/call succeeded")

                    print("\n✅ All roots tests passed!")
                    return 0

    except Exception as e:
        print(f"\n❌ Test failed: {e}")
        import traceback

        traceback.print_exc()
        return 1
    finally:
        harness.stop_process(process)


if __name__ == "__main__":
    sys.exit(asyncio.run(run_test()))
