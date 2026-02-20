#!/usr/bin/env python3
"""Test Python reference client against C++ STDIO roots fixture."""

import argparse
import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))

from python import harness
from python import stdio_raw


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpp-server", required=True)
    return parser.parse_args()


async def run_test():
    args = parse_args()
    client = None

    # Start C++ STDIO fixture
    process = harness.start_process([args.cpp_server])

    try:
        # Create STDIO raw client
        client = stdio_raw.StdioRawClient(process)

        # Register handlers for server-initiated requests
        client.register_request_handler("ping", lambda msg: {})
        # The roots fixture also sends roots/list request to the client
        client.register_request_handler("roots/list", lambda msg: {"roots": []})

        client.start()

        # Wait a moment for initialization
        await asyncio.sleep(0.5)

        # Initialize with roots capability
        print("Initializing...")
        init_response = client.send_request(
            "initialize",
            {
                "protocolVersion": "2025-11-25",
                "capabilities": {"roots": {"listChanged": True}},
                "clientInfo": {"name": "test-client", "version": "1.0.0"},
            },
        )
        assert "result" in init_response, f"Initialize failed: {init_response}"
        print("✓ Initialize succeeded")

        # Send notifications/initialized
        client.send_notification("notifications/initialized")

        # Wait longer for server to process init and any outbound requests
        await asyncio.sleep(5.0)

        # Test tools/list
        print("Testing tools/list...")
        tools_response = client.send_request("tools/list")
        assert "result" in tools_response, "tools/list failed"
        tool_names = [
            t.get("name") for t in tools_response.get("result", {}).get("tools", [])
        ]
        assert "cpp_echo" in tool_names, f"Expected cpp_echo tool: {tool_names}"
        print("✓ tools/list succeeded")

        # Test tools/call
        print("Testing tools/call...")
        call_response = client.send_request(
            "tools/call", {"name": "cpp_echo", "arguments": {"text": "hello"}}
        )
        assert "result" in call_response, "tools/call failed"
        print("✓ tools/call succeeded")

        print("\n✅ All STDIO roots tests passed!")
        return 0

    except Exception as e:
        print(f"\n❌ Test failed: {e}")
        return 1
    finally:
        if client is not None:
            try:
                client.stop()
            except Exception:
                pass
        harness.stop_process(process)


if __name__ == "__main__":
    sys.exit(asyncio.run(run_test()))
