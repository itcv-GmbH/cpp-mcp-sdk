# http_client_auth

OAuth discovery, PKCE, and loopback redirect receiver example for MCP clients.

This example demonstrates how an MCP Client handles the full OAuth 2.1 authorization flow required by secure MCP servers, including:
- **RFC9728 Protected Resource Metadata Discovery**: Finding the authorization server associated with an MCP endpoint.
- **RFC8414 Authorization Server Metadata Discovery**: Fetching the OAuth server's endpoints (authorization, token).
- **PKCE (Proof Key for Code Exchange)**: Generating secure code challenges and verifiers.
- **Loopback Redirect Receiver**: Spinning up a temporary local HTTP server to capture the authorization code callback.
- **Token Exchange**: Trading the authorization code for a Bearer token to use in subsequent MCP requests.

## Architecture & Code Flow

1. **Mock Discovery & Authorization**: The example spins up an in-process mock HTTPS server to simulate an external identity provider (like Auth0 or Keycloak).
2. **Discovery**: The client queries the mock server to discover the OAuth endpoints.
3. **Loopback Receiver**: The client starts a `LoopbackRedirectReceiver` on `127.0.0.1` on a random port.
4. **Authorization URL**: The client generates the authorization URL and prompts the user to "visit" it (in this example, it automatically simulates the callback).
5. **Callback Capture**: The loopback receiver catches the HTTP redirect containing the `code=...` parameter.
6. **Token Exchange**: The client exchanges the code and PKCE verifier for a Bearer token.
7. **Authenticated MCP Request**: The client attaches the Bearer token to the `Authorization` header of the MCP HTTP client and connects successfully.

## Security Note

For local demo purposes, this example explicitly relaxes discovery policy checks:
- `requireHttps = false`
- `allowPrivateAndLocalAddresses = true`

These settings are only for local testing against `127.0.0.1`. Keep default SSRF (Server-Side Request Forgery) protections enabled in production clients.

## Build

```bash
cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build/vcpkg-unix-release --target mcp_sdk_example_http_client_auth
```

## Run

```bash
./build/vcpkg-unix-release/examples/http_client_auth/mcp_sdk_example_http_client_auth
```