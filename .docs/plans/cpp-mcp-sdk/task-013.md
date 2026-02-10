# Task ID: [task-013]
# Task Name: [Add HTTPS (TLS) for HTTP Server + Client (Runtime Config)]

## Context
Enable HTTPS for both server and client, configurable at runtime (cert/key, CA roots, verification modes), to support OAuth flows and secure MCP sessions.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Client must support HTTPS; Server must support HTTPS)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/security_best_practices.md`

## Output / Definition of Done
* `include/mcp/transport/http.hpp` includes TLS configuration structs
* HTTP server can run in HTTP or HTTPS mode based on runtime config
* HTTP client supports HTTPS with certificate validation enabled by default
* Tests cover:
  - HTTPS handshake for local test certs
  - certificate verification failure behavior

## Step-by-Step Instructions
1. Add TLS config types:
   - server: cert chain, private key, optional client auth
   - client: CA bundle/path, verify peer, SNI
2. Implement TLS contexts (OpenSSL via Boost.Asio SSL).
3. Ensure defaults are secure (verify enabled; no insecure downgrade by default).
4. Add test fixtures for local TLS (self-signed CA) and negative verification tests.

## Verification
* `ctest --test-dir build`
