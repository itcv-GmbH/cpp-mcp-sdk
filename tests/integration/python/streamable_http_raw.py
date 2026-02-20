"""Streamable HTTP raw transport for MCP JSON-RPC."""

import json
import queue
import re
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
        self._notification_queue: queue.Queue[dict[str, Any]] = queue.Queue()
        self._response_queues: dict[str, queue.Queue[dict[str, Any]]] = {}
        self._lock = threading.Lock()
        self._running = True
        self._sse_thread: Optional[threading.Thread] = None
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

        # Send request and get response with headers
        headers = self._make_headers()
        request_id = request["id"]

        response = self._http_client.post(
            self.endpoint,
            json=request,
            headers=headers,
        )

        # Extract session ID from response headers
        session_id = response.headers.get("mcp-session-id")
        if session_id:
            self.session_id = session_id

        return response.json()

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
        request_id = request.get("id")
        if request_id is None:
            raise ValueError("Request missing 'id' field")

        response = self._http_client.post(
            self.endpoint,
            json=request,
            headers=headers,
        )

        # Update session ID from response if present
        session_id = response.headers.get("mcp-session-id")
        if session_id:
            self.session_id = session_id

        # Check if response is SSE stream
        content_type = response.headers.get("content-type", "")
        if "text/event-stream" in content_type:
            # Parse SSE stream for JSON-RPC response
            for line in response.iter_lines():
                if line.startswith("data: "):
                    data = line[6:]
                    try:
                        message = json.loads(data)
                        if message.get("id") == request_id:
                            return message
                        else:
                            # Other message (notification or related request)
                            self._handle_sse_message(message)
                    except json.JSONDecodeError:
                        pass
            raise RuntimeError("No response found in SSE stream")
        else:
            # Regular JSON response
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
        self.stop()
        self._http_client.close()

    def start_sse_listener(self) -> None:
        """Start background thread to listen for server-initiated messages via GET SSE."""
        self._sse_thread = threading.Thread(target=self._sse_listen_loop, daemon=True)
        self._sse_thread.start()

    def _sse_listen_loop(self) -> None:
        """Background thread that maintains SSE connection."""
        headers = self._make_headers()
        headers["Accept"] = "text/event-stream"

        last_event_id = None
        retry_delay = 1.0

        while self._running:
            try:
                request_headers = headers.copy()
                if last_event_id:
                    request_headers["Last-Event-ID"] = last_event_id

                with httpx.Client() as client:
                    with client.stream(
                        "GET", self.endpoint, headers=request_headers, timeout=30.0
                    ) as response:
                        if response.status_code == 405:
                            # Server doesn't support GET SSE
                            return

                        for line in response.iter_lines():
                            if line.startswith("id: "):
                                last_event_id = line[4:]
                            elif line.startswith("retry: "):
                                retry_delay = float(line[7:]) / 1000.0
                            elif line.startswith("data: "):
                                data = line[6:]
                                try:
                                    message = json.loads(data)
                                    self._handle_sse_message(message)
                                except json.JSONDecodeError:
                                    pass
                            elif line == "":
                                # Empty line marks end of event
                                pass
            except Exception:
                # Reconnect after delay
                time.sleep(retry_delay)

    def _handle_sse_message(self, message: dict) -> None:
        """Route SSE message to appropriate queue."""
        msg_id = message.get("id")
        if msg_id is None:
            # Notification
            self._notification_queue.put(message)
        else:
            # Response
            with self._lock:
                if msg_id in self._response_queues:
                    self._response_queues[msg_id].put(message)

    def stop(self) -> None:
        """Stop the SSE listener."""
        self._running = False
        if self._sse_thread and self._sse_thread.is_alive():
            self._sse_thread.join(timeout=2.0)
