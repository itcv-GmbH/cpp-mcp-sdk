# http_server_auth

Streamable HTTP MCP server over HTTPS with OAuth-style bearer authorization challenge support.

This example demonstrates:
- Streamable HTTP transport (`POST /mcp`, `GET /mcp` SSE)
- authorization via `WWW-Authenticate: Bearer ...`
- external client-driven session lifecycle (`initialize` then `notifications/initialized`)

## Build

```bash
cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build/vcpkg-unix-release --target mcp_sdk_example_http_server_auth
```

Alternative (only when dependencies are already discoverable without vcpkg):

```bash
cmake -S . -B build -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build --target mcp_sdk_example_http_server_auth
```

## Run (HTTPS required)

Generate local certs first:

```bash
chmod +x examples/http_server_auth/tls/generate_local_certs.sh
./examples/http_server_auth/tls/generate_local_certs.sh
```

Then run:

```bash
./build/vcpkg-unix-release/examples/http_server_auth/mcp_sdk_example_http_server_auth \
  --bind 127.0.0.1 \
  --port 8443 \
  --tls-cert examples/http_server_auth/tls/localhost-cert.pem \
  --tls-key examples/http_server_auth/tls/localhost-key.pem
```

`http_server_auth` requires HTTPS when authorization metadata is enabled.

## Verify Request Sequence

With the server running, execute a minimal client flow:

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
        body = response.read().decode("utf-8")
        return response.getheader("MCP-Session-Id"), response.status, body

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

## Notes

- Demo bearer tokens:
  - `dev-token-read` (`mcp:read`)
  - `dev-token-write` (`mcp:read mcp:write`)
- Authorization metadata is published by the HTTP transport at `.well-known/oauth-protected-resource` paths.
