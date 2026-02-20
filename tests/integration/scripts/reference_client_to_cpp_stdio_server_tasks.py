#!/usr/bin/env python3
"""Test Python reference client against C++ STDIO tasks fixture."""

import argparse
import asyncio
import json
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

        # Initialize with tasks capability
        print("Initializing...")
        init_response = client.send_request(
            "initialize",
            {
                "protocolVersion": "2025-11-25",
                "capabilities": {"tasks": {"listChanged": True}},
                "clientInfo": {"name": "test-client", "version": "1.0.0"},
            },
        )
        assert "result" in init_response, f"Initialize failed: {init_response}"
        print("✓ Initialize succeeded")

        # Send notifications/initialized
        client.send_notification("notifications/initialized")

        # Wait for server to process init
        await asyncio.sleep(0.5)

        # Test tools/list
        print("Testing tools/list...")
        tools_response = client.send_request("tools/list")
        assert "result" in tools_response, "tools/list failed"
        tool_names = [
            t.get("name") for t in tools_response.get("result", {}).get("tools", [])
        ]
        assert "cpp_echo" in tool_names, f"Expected cpp_echo tool: {tool_names}"
        assert "cpp_long_running_task" in tool_names, (
            f"Expected cpp_long_running_task tool: {tool_names}"
        )
        print("✓ tools/list succeeded")

        # Test tools/call with basic tool
        print("Testing tools/call (cpp_echo)...")
        call_response = client.send_request(
            "tools/call", {"name": "cpp_echo", "arguments": {"text": "hello"}}
        )
        assert "result" in call_response, "tools/call (cpp_echo) failed"
        print("✓ tools/call (cpp_echo) succeeded")

        # Test tools/call with long-running task tool
        print("Testing tools/call (cpp_long_running_task)...")
        call_response = client.send_request(
            "tools/call",
            {
                "name": "cpp_long_running_task",
                "arguments": {"text": "test", "steps": 2},
            },
        )
        assert "result" in call_response, "tools/call (cpp_long_running_task) failed"
        print("✓ tools/call (cpp_long_running_task) succeeded")

        # Test tasks/list
        print("Testing tasks/list...")
        tasks_response = client.send_request("tasks/list")
        assert "result" in tasks_response, "tasks/list failed"
        tasks = tasks_response.get("result", {}).get("tasks", [])
        # The server expects tasks/list to be called, may return empty list
        print(f"✓ tasks/list succeeded (received {len(tasks)} tasks)")

        # Test tasks/get for non-existent task (should return error or null)
        print("Testing tasks/get (non-existent)...")
        get_task_response = client.send_request(
            "tasks/get", {"taskId": "non-existent-task-id"}
        )
        # This should return a valid response (possibly with null or error)
        assert "result" in get_task_response or "error" in get_task_response, (
            "tasks/get should return result or error"
        )
        print("✓ tasks/get (non-existent) succeeded")

        # Test tasks/cancel for non-existent task (should return valid error or success)
        print("Testing tasks/cancel (non-existent)...")
        cancel_response = client.send_request(
            "tasks/cancel", {"taskId": "non-existent-task-id"}
        )
        # Should return a valid response
        assert "result" in cancel_response or "error" in cancel_response, (
            "tasks/cancel should return result or error"
        )
        print("✓ tasks/cancel (non-existent) succeeded")

        # Wait for any task status notifications from the server
        await asyncio.sleep(1.0)

        # Get any notifications
        notifications = client.get_notifications(timeout=0.5)
        task_notifications = [
            n for n in notifications if n.get("method") == "notifications/tasks/status"
        ]
        print(f"✓ Received {len(task_notifications)} task status notification(s)")

        print("\n✅ All STDIO tasks tests passed!")
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
