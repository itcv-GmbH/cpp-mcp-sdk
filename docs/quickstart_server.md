# Quickstart: MCP Server

This quickstart covers runnable server paths in this repository using the runner-based API:

- stdio server: `examples/stdio_server/` — uses `mcp::server::StdioServerRunner`
- Streamable HTTP + auth server: `examples/http_server_auth/` — uses `mcp::server::StreamableHttpServerRunner`
- dual transport server: `examples/dual_transport_server/` — uses `mcp::server::CombinedServerRunner`

## Runner Overview

The SDK provides three high-level runners that handle transport lifecycle and session management:

- **STDIO Runner** (`mcp::server::StdioServerRunner`): Guarantees no logs to stdout (logs go to stderr). Creates one `Server` instance per `run()` call.
- **HTTP Runner** (`mcp::server::StreamableHttpServerRunner`): Provides `start()`/`stop()` methods and creates one `mcp::Server` per `MCP-Session-Id` via a `ServerFactory`. Supports multi-client isolation when `requireSessionId=true`.
- **Combined Runner** (`mcp::server::CombinedServerRunner`): Supports starting STDIO, HTTP, or both in a single process. Shares a `ServerFactory` across transports.

## Prerequisites

- CMake 3.16+
- A configured vcpkg environment (`VCPKG_ROOT` set) for the `vcpkg-*` presets
- Build tools supported by your platform (Ninja on Unix presets)

## Build examples

Use the same options and presets used by the example READMEs:

```bash
cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build/vcpkg-unix-release --target mcp_sdk_example_stdio_server mcp_sdk_example_http_server_auth mcp_sdk_example_dual_transport_server
```

`MCP_SDK_BUILD_EXAMPLES` defaults to `ON`, but keeping it explicit in quickstart commands avoids confusion in custom builds.

## Run a stdio server

The stdio server example uses `mcp::server::StdioServerRunner`, which guarantees no logs go to stdout (they're written to stderr). The runner creates one `Server` instance per `run()` call and calls `start()` before processing messages.

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

The HTTP server example uses `mcp::server::StreamableHttpServerRunner`, which provides `start()`/`stop()` methods and creates one `mcp::Server` per `MCP-Session-Id` via a `ServerFactory` (when `requireSessionId=true`).

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
protocol_version = "2025-11-25"

def post(payload, session_id=None, include_protocol_version=False):
    request_headers = dict(headers)
    if session_id:
        request_headers["MCP-Session-Id"] = session_id
    if include_protocol_version:
        request_headers["MCP-Protocol-Version"] = protocol_version
    request = urllib.request.Request(
        base_url,
        data=json.dumps(payload).encode("utf-8"),
        headers=request_headers,
        method="POST",
    )
    with urllib.request.urlopen(request, context=context) as response:
        return response.getheader("MCP-Session-Id"), response.status, response.read().decode("utf-8")

# Initialize: response includes MCP-Session-Id header
session_id, status, initialize_body = post({
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
        "protocolVersion": protocol_version,
        "capabilities": {},
        "clientInfo": {"name": "manual-client", "version": "0.1.0"},
    },
})
print("initialize status:", status)
print("session id:", session_id)
print("initialize body:", initialize_body)

# Subsequent requests include both MCP-Session-Id and MCP-Protocol-Version
_, status, _ = post({"jsonrpc": "2.0", "method": "notifications/initialized"}, session_id, include_protocol_version=True)
print("notifications/initialized status:", status)

_, status, tools_body = post({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}}, session_id, include_protocol_version=True)
print("tools/list status:", status)
print("tools/list body:", tools_body)
PY
```

Demo bearer tokens in this example:

- `dev-token-read` (`mcp:read`)
- `dev-token-write` (`mcp:read mcp:write`)

## Run a dual transport server (STDIO + HTTP)

The dual transport example uses `mcp::server::CombinedServerRunner` to run both STDIO and HTTP transports in a single process. Both transports share a `ServerFactory` that creates fresh `mcp::Server` instances per session.

Start the example:

```bash
./build/vcpkg-unix-release/examples/dual_transport_server/mcp_sdk_example_dual_transport_server
```

The server starts:
- HTTP server on `127.0.0.1:8080/mcp` (background thread)
- STDIO transport reading from stdin (foreground, blocking)

The example demonstrates:
- Shared `ServerFactory` across transports
- Per-session server isolation for HTTP (when `requireSessionId=true`)
- Graceful shutdown on SIGINT

## Legacy 2024-11-05 HTTP+SSE server compatibility (optional)

Servers can expose legacy `GET /events` + `POST /rpc` endpoints alongside modern Streamable HTTP when interoperability is required.

- Build default: `MCP_SDK_ENABLE_LEGACY_HTTP_SSE_SERVER_COMPATIBILITY=OFF`
- Runtime default: `StreamableHttpServerOptions.enableLegacyHttpSseCompatibility = std::nullopt` (inherits build default)

Enable per server instance:

```cpp
mcp::transport::http::StreamableHttpServerOptions options;
options.enableLegacyHttpSseCompatibility = true;
options.legacySseEndpointPath = "/events";
options.legacyPostEndpointPath = "/rpc";
```

When enabled, `GET /events` sends an initial SSE `endpoint` event containing the POST URI, and server JSON-RPC messages are emitted as SSE `message` events.

Use this only for legacy-client interop and prefer modern Streamable HTTP when possible.

## Relevant CMake options

- `MCP_SDK_BUILD_EXAMPLES=ON` builds example binaries.
- `MCP_SDK_ENABLE_TLS=ON` is enabled by default.
- `MCP_SDK_ENABLE_LEGACY_HTTP_SSE_SERVER_COMPATIBILITY=OFF` keeps legacy HTTP+SSE server endpoints disabled by default.
- `MCP_SDK_BUILD_TESTS=ON` is unrelated to this quickstart and can be disabled for faster local builds.
