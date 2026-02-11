#!/usr/bin/env python3

import argparse
import json
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

import httpx


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run C++ SDK client fixture against reference Python server"
    )
    parser.add_argument(
        "--cpp-client", required=True, help="Path to C++ client fixture executable"
    )
    parser.add_argument(
        "--reference-server-script",
        required=True,
        help="Path to reference Python server fixture script",
    )
    return parser.parse_args()


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def initialize_probe_payload() -> dict[str, Any]:
    return {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2025-11-25",
            "capabilities": {},
            "clientInfo": {
                "name": "cpp-probe",
                "version": "1.0.0",
            },
        },
    }


def read_process_output(process: subprocess.Popen[str]) -> str:
    if process.stdout is None:
        return ""
    return process.stdout.read()


def stop_server(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return

    process.terminate()
    try:
        process.wait(timeout=5)
        return
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def wait_for_server_ready(
    endpoint: str, process: subprocess.Popen[str], timeout_seconds: float = 25.0
) -> None:
    payload = initialize_probe_payload()
    deadline = time.monotonic() + timeout_seconds

    while time.monotonic() < deadline:
        if process.poll() is not None:
            output = read_process_output(process)
            raise RuntimeError(
                f"Reference Python server exited before readiness (exit={process.returncode}).\n{output}"
            )

        try:
            response = httpx.post(endpoint, json=payload, timeout=1.0)
            if response.status_code in (400, 401, 403, 405):
                return
        except Exception:
            pass

        time.sleep(0.1)

    output = read_process_output(process)
    raise TimeoutError(
        f"Timed out waiting for reference server readiness at {endpoint}.\n{output}"
    )


def run_cpp_client(command: list[str]) -> tuple[int, str]:
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    combined_output = (completed.stdout or "") + (completed.stderr or "")
    return completed.returncode, combined_output


def run() -> int:
    args = parse_args()
    cpp_client = str(Path(args.cpp_client).resolve())
    reference_server_script = str(Path(args.reference_server_script).resolve())

    token = "integration-token"
    host = "127.0.0.1"
    path = "/mcp"
    port = find_free_port()
    endpoint = f"http://{host}:{port}{path}"

    server_process = subprocess.Popen(
        [
            sys.executable,
            reference_server_script,
            "--host",
            host,
            "--port",
            str(port),
            "--path",
            path,
            "--token",
            token,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    try:
        wait_for_server_ready(endpoint, server_process)

        unauthorized_command = [
            cpp_client,
            "--endpoint",
            endpoint,
            "--expect-unauthorized",
        ]
        unauthorized_code, unauthorized_output = run_cpp_client(unauthorized_command)
        if unauthorized_code != 0:
            raise RuntimeError(
                "C++ client did not observe expected unauthorized behavior.\n"
                f"command: {' '.join(unauthorized_command)}\n"
                f"exit: {unauthorized_code}\n"
                f"output:\n{unauthorized_output}"
            )

        authorized_command = [
            cpp_client,
            "--endpoint",
            endpoint,
            "--token",
            token,
        ]
        authorized_code, authorized_output = run_cpp_client(authorized_command)
        if authorized_code != 0:
            raise RuntimeError(
                "C++ client failed authenticated interoperability flow.\n"
                f"command: {' '.join(authorized_command)}\n"
                f"exit: {authorized_code}\n"
                f"output:\n{authorized_output}"
            )

        return 0
    finally:
        stop_server(server_process)


if __name__ == "__main__":
    exit_code = run()
    print(json.dumps({"result": "ok", "script": "cpp_client_to_reference_server"}))
    raise SystemExit(exit_code)
