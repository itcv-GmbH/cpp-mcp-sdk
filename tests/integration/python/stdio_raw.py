"""STDIO raw transport for MCP JSON-RPC."""

import json
import queue
import subprocess
import threading
import time
from typing import Any, Callable, Dict, IO, Optional


class StdioRawClient:
    """Raw JSON-RPC client for STDIO transport."""

    def __init__(
        self,
        process: subprocess.Popen[str],
        timeout: float = 10.0,
    ):
        self.process = process
        self.timeout = timeout
        self._notification_queue: queue.Queue[dict[str, Any]] = queue.Queue()
        self._response_queues: dict[str | int, queue.Queue[dict[str, Any]]] = {}
        self._lock = threading.Lock()
        self._reader_thread: Optional[threading.Thread] = None
        self._running = False
        # Handler for server-initiated requests (e.g., ping, roots/list from server)
        self._request_handlers: Dict[
            str, Callable[[dict[str, Any]], dict[str, Any]]
        ] = {}

    def register_request_handler(
        self, method: str, handler: Callable[[dict[str, Any]], dict[str, Any]]
    ) -> None:
        """Register a handler for server-initiated requests."""
        self._request_handlers[method] = handler

    def start(self) -> None:
        """Start the reader thread."""
        self._running = True
        self._reader_thread = threading.Thread(target=self._read_loop)
        self._reader_thread.start()

    def stop(self) -> None:
        """Stop the reader thread and signal EOF to server."""
        self._running = False

        # Note: We don't close stdin here because the test harness handles process cleanup
        # and closing stdin may cause broken pipe errors if the server has already closed it

        if self._reader_thread:
            self._reader_thread.join(timeout=1.0)

    def _read_loop(self) -> None:
        """Background thread that reads from stdout."""
        stdout: Optional[IO[str]] = self.process.stdout
        while self._running and self.process.poll() is None:
            try:
                if stdout is None:
                    continue
                line = stdout.readline()
                if not line:
                    continue

                # Check if it's a JSON line
                stripped = line.strip()
                if not stripped:
                    continue
                try:
                    message = json.loads(stripped)
                    self._handle_message(message)
                except json.JSONDecodeError:
                    # This is a non-JSON line (stderr log output)
                    continue
            except Exception:
                # Log unexpected errors silently
                continue

    def _handle_message(self, message: dict[str, Any]) -> None:
        """Route incoming messages to appropriate handlers."""
        msg_method = message.get("method")
        msg_id = message.get("id")

        # Handle server-initiated requests first (these have both method and id)
        if msg_method is not None and msg_id is not None:
            # This is a server-initiated request (e.g., ping, roots/list from server)
            # The server expects a response from the client
            response_result: dict[str, Any] = {}
            if msg_method in self._request_handlers:
                handler = self._request_handlers[msg_method]
                try:
                    response_result = handler(message)
                except Exception:
                    # Send error response
                    error_response = {
                        "jsonrpc": "2.0",
                        "id": msg_id,
                        "error": {
                            "code": -32603,
                            "message": "Internal error",
                        },
                    }
                    self._send_message(error_response)
                    return

            # Send success response
            response = {
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": response_result,
            }
            self._send_message(response)
            return  # Don't process this as a response

        if msg_id is None:
            # This is a notification
            self._notification_queue.put(message)
        else:
            # This is a response - handle both string and numeric IDs
            with self._lock:
                # Check with original type
                if msg_id in self._response_queues:
                    self._response_queues[msg_id].put(message)
                # Also check with converted type (string <-> int)
                elif isinstance(msg_id, str):
                    try:
                        num_id = int(msg_id)
                        if num_id in self._response_queues:
                            self._response_queues[num_id].put(message)
                    except ValueError:
                        pass
                elif isinstance(msg_id, int):
                    str_id = str(msg_id)
                    if str_id in self._response_queues:
                        self._response_queues[str_id].put(message)

    def send_request(
        self, method: str, params: Optional[dict[str, Any]] = None
    ) -> dict[str, Any]:
        """Send a JSON-RPC request and return the response."""
        # Use a unique ID based on time to avoid collision with server's outbound requests
        # The server may use small integers (1, 2, 3) for its outbound requests, so we use
        # a different range based on the current time in milliseconds
        request_id = int(time.time() * 1000) % 100000

        request: dict[str, Any] = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
        }
        if params is not None:
            request["params"] = params

        # Create response queue for this request
        response_queue: queue.Queue[dict[str, Any]] = queue.Queue()
        with self._lock:
            self._response_queues[request_id] = response_queue

        # Send the request
        self._send_message(request)

        # Wait for response
        try:
            response = response_queue.get(timeout=self.timeout)
            return response
        finally:
            with self._lock:
                del self._response_queues[request_id]

    def send_notification(
        self, method: str, params: Optional[dict[str, Any]] = None
    ) -> None:
        """Send a JSON-RPC notification."""
        notification: dict[str, Any] = {
            "jsonrpc": "2.0",
            "method": method,
        }
        if params is not None:
            notification["params"] = params

        self._send_message(notification)

    def _send_message(self, message: dict[str, Any]) -> None:
        """Send a message to stdin."""
        stdin: Optional[IO[str]] = self.process.stdin
        if stdin is None:
            raise RuntimeError("Process stdin is not available")
        line = json.dumps(message) + "\n"
        stdin.write(line)
        stdin.flush()

    def get_notifications(
        self, timeout: Optional[float] = None
    ) -> list[dict[str, Any]]:
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
