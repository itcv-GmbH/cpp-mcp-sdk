# Task-001 Inventory: Namespace-to-Path Violations

**Task:** Inventory `src/**/*.cpp` namespace-to-path violations  
**Completed:** 2026-02-23  
**Analyzed Files:** 24

## Rule Applied (FR2)

- Expected namespace for `src/<seg1>/<seg2>/.../<segn>/file.cpp` is `mcp::<seg1>::<seg2>::...::<segn>`
- Filename is excluded from namespace derivation
- For each file, inventory uses the first non-anonymous MCP namespace opened

## Violations

Total violations found: **5**

| # | File Path | Namespace Line | First MCP Namespace Opened | Expected Namespace |
|---|-----------|----------------|----------------------------|--------------------|
| 1 | `src/server/server.cpp` | 85 | `mcp::server::detail` | `mcp::server` |
| 2 | `src/transport/http_client.cpp` | 42 | `mcp::transport::http` | `mcp::transport` |
| 3 | `src/transport/http_runtime.cpp` | 38 | `mcp::transport::http` | `mcp::transport` |
| 4 | `src/transport/http_server.cpp` | 61 | `mcp::transport::http` | `mcp::transport` |
| 5 | `src/version.cpp` | 4 | `mcp::sdk` | `mcp` |

## Full Scan (All `src/**/*.cpp`)

| # | File Path | Namespace Line | First MCP Namespace Opened | Expected Namespace | Status |
|---|-----------|----------------|----------------------------|--------------------|--------|
| 1 | `src/auth/client_registration.cpp` | 40 | `mcp::auth` | `mcp::auth` | OK |
| 2 | `src/auth/loopback_receiver.cpp` | 21 | `mcp::auth` | `mcp::auth` | OK |
| 3 | `src/auth/oauth_client.cpp` | 30 | `mcp::auth` | `mcp::auth` | OK |
| 4 | `src/auth/oauth_client_disabled.cpp` | 5 | `mcp::auth` | `mcp::auth` | OK |
| 5 | `src/auth/protected_resource_metadata.cpp` | 25 | `mcp::auth` | `mcp::auth` | OK |
| 6 | `src/client/client.cpp` | 104 | `mcp::client` | `mcp::client` | OK |
| 7 | `src/detail/inbound_loop.cpp` | 10 | `mcp::detail` | `mcp::detail` | OK |
| 8 | `src/detail/initialize_codec.cpp` | 14 | `mcp::detail` | `mcp::detail` | OK |
| 9 | `src/jsonrpc/messages.cpp` | 28 | `mcp::jsonrpc` | `mcp::jsonrpc` | OK |
| 10 | `src/jsonrpc/router.cpp` | 40 | `mcp::jsonrpc` | `mcp::jsonrpc` | OK |
| 11 | `src/lifecycle/session.cpp` | 52 | `mcp::lifecycle` | `mcp::lifecycle` | OK |
| 12 | `src/schema/validator.cpp` | 19 | `mcp::schema` | `mcp::schema` | OK |
| 13 | `src/security/crypto_random.cpp` | 22 | `mcp::security` | `mcp::security` | OK |
| 14 | `src/server/combined_runner.cpp` | 28 | `mcp::server` | `mcp::server` | OK |
| 15 | `src/server/server.cpp` | 85 | `mcp::server::detail` | `mcp::server` | VIOLATION |
| 16 | `src/server/stdio_runner.cpp` | 38 | `mcp::server` | `mcp::server` | OK |
| 17 | `src/server/streamable_http_runner.cpp` | 47 | `mcp::server` | `mcp::server` | OK |
| 18 | `src/transport/http_client.cpp` | 42 | `mcp::transport::http` | `mcp::transport` | VIOLATION |
| 19 | `src/transport/http_runtime.cpp` | 38 | `mcp::transport::http` | `mcp::transport` | VIOLATION |
| 20 | `src/transport/http_server.cpp` | 61 | `mcp::transport::http` | `mcp::transport` | VIOLATION |
| 21 | `src/transport/stdio.cpp` | 59 | `mcp::transport` | `mcp::transport` | OK |
| 22 | `src/transport/streamable_http_client_transport.cpp` | 23 | `mcp::transport` | `mcp::transport` | OK |
| 23 | `src/util/tasks.cpp` | 47 | `mcp::util` | `mcp::util` | OK |
| 24 | `src/version.cpp` | 4 | `mcp::sdk` | `mcp` | VIOLATION |
