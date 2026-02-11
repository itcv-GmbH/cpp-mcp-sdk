# http_client_auth

OAuth discovery + PKCE + loopback redirect receiver example for MCP clients.

This example spins up a local mock authorization server and demonstrates:
- RFC9728 protected resource metadata discovery
- RFC8414 authorization server metadata discovery
- PKCE authorization URL generation
- loopback redirect receiver for auth code capture
- token exchange request execution

## Build

```bash
cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build/vcpkg-unix-release --target mcp_sdk_example_http_client_auth
```

## Run

```bash
./build/vcpkg-unix-release/examples/http_client_auth/mcp_sdk_example_http_client_auth
```

The example is self-contained and runs entirely against a local mock server.
