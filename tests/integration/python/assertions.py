"""Reusable assertions for MCP JSON-RPC testing."""

from typing import Any, Optional


def assert_jsonrpc_response_shape(response: dict[str, Any], expected_id: Any) -> None:
    """Assert that a response has valid JSON-RPC shape."""
    assert "jsonrpc" in response, "Response missing jsonrpc field"
    assert response["jsonrpc"] == "2.0", (
        f"Invalid jsonrpc version: {response['jsonrpc']}"
    )
    assert "id" in response, "Response missing id field"
    assert response["id"] == expected_id, (
        f"Response id mismatch: {response['id']} != {expected_id}"
    )

    has_result = "result" in response
    has_error = "error" in response
    assert has_result or has_error, "Response must have either result or error"
    assert not (has_result and has_error), "Response cannot have both result and error"


def assert_mcp_error(
    response: dict[str, Any],
    expected_code: int,
    expected_message_substring: Optional[str] = None,
) -> None:
    """Assert that a response is a JSON-RPC error with expected code."""
    assert "error" in response, f"Expected error response, got: {response}"
    error = response["error"]
    assert "code" in error, "Error missing code field"
    assert error["code"] == expected_code, (
        f"Error code mismatch: {error['code']} != {expected_code}"
    )

    if expected_message_substring:
        assert "message" in error, "Error missing message field"
        assert expected_message_substring.lower() in error["message"].lower(), (
            f"Error message doesn't contain '{expected_message_substring}': {error['message']}"
        )


def assert_notification_name(notification: dict[str, Any], expected_name: str) -> None:
    """Assert that a notification has the expected method name."""
    assert "method" in notification, (
        f"Notification missing method field: {notification}"
    )
    assert notification["method"] == expected_name, (
        f"Notification method mismatch: {notification['method']} != {expected_name}"
    )


def assert_success_response(response: dict[str, Any]) -> dict[str, Any]:
    """Assert that a response is a success and return the result."""
    assert "result" in response, (
        f"Expected success response, got error: {response.get('error')}"
    )
    return response["result"]


def assert_capability_declared(capabilities: dict[str, Any], capability: str) -> None:
    """Assert that a capability is declared."""
    assert isinstance(capabilities, dict), (
        f"capabilities must be a dict, got {type(capabilities)}"
    )
    assert capability in capabilities, (
        f"Capability '{capability}' not declared. Available: {list(capabilities.keys())}"
    )
