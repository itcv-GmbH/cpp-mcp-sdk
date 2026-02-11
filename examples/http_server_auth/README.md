# http_server_auth

Streamable HTTP MCP server with optional HTTPS and OAuth-style bearer authorization challenge support.

This example demonstrates:
- Streamable HTTP transport (`POST /mcp`, `GET /mcp` SSE)
- authorization via `WWW-Authenticate: Bearer ...`
- optional TLS runtime configuration

## Build

```bash
cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build/vcpkg-unix-release --target mcp_sdk_example_http_server_auth
```

## Run (HTTP)

```bash
./build/vcpkg-unix-release/examples/http_server_auth/mcp_sdk_example_http_server_auth --bind 127.0.0.1 --port 8080
```

## Run (HTTPS)

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

## Notes

- Demo bearer tokens:
  - `dev-token-read`
  - `dev-token-write`
- Authorization metadata is published by the HTTP transport at `.well-known/oauth-protected-resource` paths.
