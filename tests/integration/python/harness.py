"""Process lifecycle helpers for integration testing."""

import queue
import select
import socket
import subprocess
import time
from typing import Optional


def find_free_port() -> int:
    """Find a free TCP port on localhost."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def start_process(
    cmd: list[str],
    cwd: Optional[str] = None,
    env: Optional[dict] = None,
) -> subprocess.Popen[str]:
    """Start a subprocess with proper configuration."""
    return subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        cwd=cwd,
        env=env,
    )


def stop_process(process: subprocess.Popen[str], timeout: float = 5.0) -> None:
    """Stop a process gracefully with escalation."""
    if process.stdin and not process.stdin.closed:
        process.stdin.close()

    try:
        process.wait(timeout=timeout)
        return
    except subprocess.TimeoutExpired:
        process.terminate()

    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=timeout)


def wait_for_readiness(
    process: subprocess.Popen[str],
    marker: str,
    timeout: float = 20.0,
) -> str:
    """Wait for a readiness marker in process stdout."""
    deadline = time.monotonic() + timeout
    output_lines = []

    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(
                f"Process exited before readiness (exit={process.returncode}). "
                f"Output: {''.join(output_lines)}"
            )

        # Read available output
        if process.stdout and select.select([process.stdout], [], [], 0.1)[0]:
            line = process.stdout.readline()
            if line:
                output_lines.append(line)
                if marker in line:
                    return line.strip()

    raise TimeoutError(
        f"Timed out waiting for readiness marker '{marker}'. "
        f"Output: {''.join(output_lines)}"
    )
