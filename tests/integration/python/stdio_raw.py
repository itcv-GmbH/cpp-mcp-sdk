"""STDIO raw transport for MCP JSON-RPC."""

import json
import queue
import subprocess
import threading
import time
import uuid
from typing import Any, IO, Optional


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
        self._response_queues: dict[str, queue.Queue[dict[str, Any]]] = {}
        self._lock = threading.Lock()
        self._reader_thread: Optional[threading.Thread] = None
        self._running = False

    def start(self) -> None:
        """Start the reader thread."""
        self._running = True
        self._reader_thread = threading.Thread(target=self._read_loop)
        self._reader_thread.start()

    def stop(self) -> None:
        """Stop the reader thread."""
        self._running = False
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

                message = json.loads(line.strip())
                self._handle_message(message)
            except json.JSONDecodeError:
                # Skip non-JSON lines (these might be log output)
                continue
            except Exception:
                continue

    def _handle_message(self, message: dict[str, Any]) -> None:
        """Route incoming messages to appropriate handlers."""
        msg_id = message.get("id")

        if msg_id is None:
            # This is a notification
            self._notification_queue.put(message)
        else:
            # This is a response
            with self._lock:
                if msg_id in self._response_queues:
                    self._response_queues[msg_id].put(message)

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
