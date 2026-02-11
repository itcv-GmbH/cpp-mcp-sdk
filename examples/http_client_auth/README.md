# http_client_auth

OAuth discovery + PKCE + loopback redirect receiver example for MCP clients.

This example uses in-process mock authorization/discovery responses and demonstrates:
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

The example is self-contained and runs entirely on localhost. The only live HTTP listener is the loopback redirect receiver used to capture the authorization code callback.

## Security Note

For local demo purposes, this example explicitly relaxes discovery policy checks:
- `requireHttps = false`
- `allowPrivateAndLocalAddresses = true`

These settings are only for local testing against `127.0.0.1`. Keep default SSRF protections enabled in production clients.
