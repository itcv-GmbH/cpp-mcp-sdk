# http_server_auth

A Streamable HTTP MCP server running over HTTPS with OAuth-style bearer authorization challenge support.

This example demonstrates how to build a secure, production-ready MCP server that requires clients to authenticate using Bearer tokens:
- **Streamable HTTP transport**: Handles `POST /mcp` for requests and `GET /mcp` (Server-Sent Events) for server-to-client messages.
- **Authorization via `WWW-Authenticate`**: Challenges unauthenticated requests with `WWW-Authenticate: Bearer ...`.
- **Token Verification**: Uses a custom `OAuthTokenVerifier` to validate Bearer tokens and enforce scope requirements (e.g., `mcp:read`, `mcp:write`).
- **External Client-Driven Session Lifecycle**: Requires the standard `initialize` followed by `notifications/initialized` sequence.

## Code Flow

1. **Token Verifier**: Implements a custom `ExampleTokenVerifier` that checks if the incoming Bearer token is valid and has the required scopes.
2. **Server Configuration**: Configures the `StreamableHttpServer` with `OAuthServerAuthorizationOptions`, pointing to the authorization server and defining required scopes.
3. **TLS/HTTPS Setup**: Loads the provided SSL certificate and private key to start the server securely. (OAuth Bearer tokens MUST be transmitted over HTTPS).
4. **Event Loop**: Starts the Boost.Asio event loop to listen for incoming HTTP/HTTPS connections.

## Build

```bash
cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build/vcpkg-unix-release --target mcp_sdk_example_http_server_auth
```

## Run (HTTPS required)

Because authorization metadata and Bearer tokens require encryption, you must generate local TLS certificates first:

```bash
chmod +x examples/http_server_auth/tls/generate_local_certs.sh
./examples/http_server_auth/tls/generate_local_certs.sh
```

Then run the server:

```bash
./build/vcpkg-unix-release/examples/http_server_auth/mcp_sdk_example_http_server_auth \
  --bind 127.0.0.1 \
  --port 8443 \
  --tls-cert examples/http_server_auth/tls/localhost-cert.pem \
  --tls-key examples/http_server_auth/tls/localhost-key.pem
```

## Verify Request Sequence

With the server running, you can execute a minimal client flow using Python to see the authorization challenge and successful token usage:

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

# 1. Initialize
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

# 2. Initialized Notification
_, status, _ = post({"jsonrpc": "2.0", "method": "notifications/initialized"}, session_id)
print("notifications/initialized status:", status)

# 3. Call Tool (Requires Auth)
_, status, tools_body = post({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}}, session_id)
print("tools/list status:", status)
print("tools/list body:", tools_body)
PY
```