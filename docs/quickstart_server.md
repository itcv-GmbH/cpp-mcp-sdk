# Quickstart: MCP Server

This quickstart covers two runnable server paths in this repository:

- stdio server: `examples/stdio_server/`
- Streamable HTTP + auth server: `examples/http_server_auth/`

## Prerequisites

- CMake 3.16+
- A configured vcpkg environment (`VCPKG_ROOT` set) for the `vcpkg-*` presets
- Build tools supported by your platform (Ninja on Unix presets)

## Build examples

Use the same options and presets used by the example READMEs:

```bash
cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build/vcpkg-unix-release --target mcp_sdk_example_stdio_server mcp_sdk_example_http_server_auth
```

`MCP_SDK_BUILD_EXAMPLES` defaults to `ON`, but keeping it explicit in quickstart commands avoids confusion in custom builds.

## Run a stdio server

Start the example:

```bash
./build/vcpkg-unix-release/examples/stdio_server/mcp_sdk_example_stdio_server
```

The server reads newline-delimited JSON-RPC from `stdin` and writes JSON-RPC responses to `stdout`.

One-shot smoke test:

```bash
printf '%s\n' \
'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"manual-client","version":"0.1.0"}}}' \
'{"jsonrpc":"2.0","method":"notifications/initialized"}' \
'{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' \
| ./build/vcpkg-unix-release/examples/stdio_server/mcp_sdk_example_stdio_server
```

## Run an HTTPS Streamable HTTP server with bearer auth

Generate local certificates:

```bash
chmod +x examples/http_server_auth/tls/generate_local_certs.sh
./examples/http_server_auth/tls/generate_local_certs.sh
```

Start the server:

```bash
./build/vcpkg-unix-release/examples/http_server_auth/mcp_sdk_example_http_server_auth \
  --bind 127.0.0.1 \
  --port 8443 \
  --path /mcp \
  --tls-cert examples/http_server_auth/tls/localhost-cert.pem \
  --tls-key examples/http_server_auth/tls/localhost-key.pem
```

The example enforces HTTPS (`--tls-cert` and `--tls-key` are required).

## Verify auth and MCP flow

With the server running:

```bash
python3 - <<'PY'
import json
import ssl
import urllib.request

base_url = "https://127.0.0.1:8443/mcp"
headers = {
    "Content-Type": "application/json",
    "Authorization": "Bearer dev-token-read",
}

context = ssl._create_unverified_context()

def post(payload, session_id=None):
    request_headers = dict(headers)
    if session_id:
        request_headers["MCP-Session-Id"] = session_id
    request = urllib.request.Request(
        base_url,
        data=json.dumps(payload).encode("utf-8"),
        headers=request_headers,
        method="POST",
    )
    with urllib.request.urlopen(request, context=context) as response:
        return response.getheader("MCP-Session-Id"), response.status, response.read().decode("utf-8")

session_id, status, initialize_body = post({
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
        "protocolVersion": "2025-11-25",
        "capabilities": {},
        "clientInfo": {"name": "manual-client", "version": "0.1.0"},
    },
})
print("initialize status:", status)
print("session id:", session_id)
print("initialize body:", initialize_body)

_, status, _ = post({"jsonrpc": "2.0", "method": "notifications/initialized"}, session_id)
print("notifications/initialized status:", status)

_, status, tools_body = post({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}}, session_id)
print("tools/list status:", status)
print("tools/list body:", tools_body)
PY
```

Demo bearer tokens in this example:

- `dev-token-read` (`mcp:read`)
- `dev-token-write` (`mcp:read mcp:write`)

## Relevant CMake options

- `MCP_SDK_BUILD_EXAMPLES=ON` builds example binaries.
- `MCP_SDK_ENABLE_TLS=ON` is enabled by default.
- `MCP_SDK_BUILD_TESTS=ON` is unrelated to this quickstart and can be disabled for faster local builds.
