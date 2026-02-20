"""Streamable HTTP raw transport for MCP JSON-RPC."""

import json
import queue
import threading
import time
import uuid
from typing import Any, Optional

import httpx


class StreamableHttpRawClient:
    """Raw JSON-RPC client for Streamable HTTP transport."""

    def __init__(
        self,
        endpoint: str,
        token: Optional[str] = None,
        timeout: float = 10.0,
    ):
        self.endpoint = endpoint
        self.token = token
        self.timeout = timeout
        self.session_id: Optional[str] = None
        self.protocol_version = "2025-11-25"
        self._pending_requests: dict[str, Any] = {}
        self._notification_queue: queue.Queue[dict[str, Any]] = queue.Queue()
        self._response_queues: dict[str, queue.Queue[dict[str, Any]]] = {}
        self._lock = threading.Lock()
        self._http_client = httpx.Client(
            headers=self._make_headers(),
            timeout=httpx.Timeout(timeout),
        )

    def _make_headers(self) -> dict[str, str]:
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
            "MCP-Protocol-Version": self.protocol_version,
        }
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        if self.session_id:
            headers["MCP-Session-Id"] = self.session_id
        return headers

    def initialize(self) -> dict:
        """Send initialize request and return response."""
        request = {
            "jsonrpc": "2.0",
            "id": str(uuid.uuid4()),
            "method": "initialize",
            "params": {
                "protocolVersion": self.protocol_version,
                "capabilities": {},
                "clientInfo": {"name": "raw-harness", "version": "1.0.0"},
            },
        }
        response = self._send_request(request)
        # Extract session ID from response headers if present
        return response

    def send_request(
        self, method: str, params: Optional[dict[str, Any]] = None
    ) -> dict[str, Any]:
        """Send a JSON-RPC request and return the response."""
        request_id = str(uuid.uuid4())
        request: dict[str, Any] = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
        }
        if params is not None:
            request["params"] = params

        return self._send_request(request)

    def send_notification(
        self, method: str, params: Optional[dict[str, Any]] = None
    ) -> None:
        """Send a JSON-RPC notification (no response expected)."""
        notification: dict[str, Any] = {
            "jsonrpc": "2.0",
            "method": method,
        }
        if params is not None:
            notification["params"] = params

        headers = self._make_headers()
        response = self._http_client.post(
            self.endpoint,
            json=notification,
            headers=headers,
        )
        # Notifications return 202 Accepted on success

    def _send_request(self, request: dict) -> dict:
        """Internal method to send request and handle response."""
        headers = self._make_headers()
        request_id = request["id"]

        response = self._http_client.post(
            self.endpoint,
            json=request,
            headers=headers,
        )

        # Update session ID from response if present
        if "mcp-session-id" in response.headers:
            self.session_id = response.headers["mcp-session-id"]

        return response.json()

    def get_notifications(self, timeout: Optional[float] = None) -> list[dict]:
        """Get accumulated notifications."""
        notifications = []
        deadline = time.monotonic() + timeout if timeout else None

        while True:
            if deadline and time.monotonic() > deadline:
                break

            try:
                remaining = max(0, deadline - time.monotonic()) if deadline else 0.1
                notification = self._notification_queue.get(timeout=min(remaining, 0.1))
                notifications.append(notification)
            except queue.Empty:
                if not timeout:
                    break

        return notifications

    def close(self) -> None:
        """Close the HTTP client."""
        self._http_client.close()
