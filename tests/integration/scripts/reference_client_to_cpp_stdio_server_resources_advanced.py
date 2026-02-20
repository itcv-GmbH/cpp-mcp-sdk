#!/usr/bin/env python3
"""Test Python reference client against C++ STDIO resources advanced fixture."""

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

        # Register handler for ping requests from server
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

        # Wait for server to process init
        await asyncio.sleep(2.0)

        # Test resources/list
        print("Testing resources/list...")
        resources_response = client.send_request("resources/list")
        assert "result" in resources_response, "resources/list failed"
        resource_uris = [
            r.get("uri")
            for r in resources_response.get("result", {}).get("resources", [])
        ]
        assert "resource://cpp-stdio-server/info" in resource_uris, (
            f"Expected cpp-stdio-server/info: {resource_uris}"
        )
        print("✓ resources/list succeeded")

        # Test resources/read
        print("Testing resources/read...")
        read_response = client.send_request(
            "resources/read", {"uri": "resource://cpp-stdio-server/info"}
        )
        assert "result" in read_response, "resources/read failed"
        contents = read_response.get("result", {}).get("contents", [])
        assert len(contents) > 0, "Expected resource contents"
        print("✓ resources/read succeeded")

        # Test resources/templates/list
        print("Testing resources/templates/list...")
        templates_response = client.send_request("resources/templates/list")
        assert "result" in templates_response, "resources/templates/list failed"
        templates = templates_response.get("result", {}).get("resourceTemplates", [])
        assert len(templates) > 0, "Expected resource templates"
        template_uri_templates = [t.get("uriTemplate") for t in templates]
        assert "resource://cpp-stdio-server/user/{userId}" in template_uri_templates, (
            f"Expected user template: {template_uri_templates}"
        )
        print("✓ resources/templates/list succeeded")

        # Test resources/subscribe
        print("Testing resources/subscribe...")
        sub_response = client.send_request(
            "resources/subscribe", {"uri": "resource://cpp-stdio-server/dynamic"}
        )
        assert "result" in sub_response, "resources/subscribe failed"
        print("✓ resources/subscribe succeeded")

        # Test resources/unsubscribe
        print("Testing resources/unsubscribe...")
        unsub_response = client.send_request(
            "resources/unsubscribe", {"uri": "resource://cpp-stdio-server/dynamic"}
        )
        assert "result" in unsub_response, "resources/unsubscribe failed"
        print("✓ resources/unsubscribe succeeded")

        print("\n✅ All STDIO resources advanced tests passed!")
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
