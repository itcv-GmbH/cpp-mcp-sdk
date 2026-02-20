#!/usr/bin/env python3
"""Test Python reference client against C++ STDIO utilities fixture."""

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

        # Register handler for ping requests from server (server may initiate ping)
        client.register_request_handler("ping", lambda msg: {})

        client.start()

        # Wait a moment for initialization
        await asyncio.sleep(0.5)

        # Initialize
        print("Initializing...")
        init_response = client.send_request(
            "initialize",
            {
                "protocolVersion": "2025-11-25",
                "capabilities": {},
                "clientInfo": {"name": "test-client", "version": "1.0.0"},
            },
        )
        assert "result" in init_response, f"Initialize failed: {init_response}"
        print("✓ Initialize succeeded")

        # Send notifications/initialized
        client.send_notification("notifications/initialized")

        # Wait for server to finish initialization and any outbound requests
        await asyncio.sleep(2.0)

        # Test ping
        print("Testing ping...")
        ping_response = client.send_request("ping")
        assert "result" in ping_response, "Ping failed"
        print("✓ Ping succeeded")

        # Wait for any pending server responses
        await asyncio.sleep(1.0)

        # Test completion/complete
        print("Testing completion/complete...")
        comp_response = client.send_request(
            "completion/complete",
            {
                "ref": {"type": "ref/prompt", "name": "cpp_stdio_server_prompt"},
                "argument": {"name": "topic", "value": "test"},
            },
        )
        assert "result" in comp_response, "completion/complete failed"
        print("✓ completion/complete succeeded")

        print("\n✅ All STDIO utilities tests passed!")
        return 0

    except Exception as e:
        print(f"\n❌ Test failed: {e}")
        return 1
    finally:
        if client is not None:
            client.stop()
        harness.stop_process(process)


if __name__ == "__main__":
    sys.exit(asyncio.run(run_test()))
