# Task ID: task-015
# Task Name: Add Transport Conformance Tests for Session Issuance + Multi-Session Routing

## Context
The transport layer must be spec-conformant for Streamable HTTP session management. Adding explicit tests for server-issued `MCP-Session-Id` ensures:
- the SDK aligns with the MCP Streamable HTTP Session Management section
- runners will implement per-session `mcp::Server` instances

## Inputs
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md` (Session Management; Multiple Connections)
* `tests/conformance/test_streamable_http_transport.cpp`
* `include/mcp/transport/http.hpp`
* Implementation from `task-014`

## Output / Definition of Done
* `tests/conformance/test_streamable_http_transport.cpp` updated with new sections that verify:
  * when `options.http.requireSessionId=true`, a successful initialize response includes `MCP-Session-Id`
  * subsequent non-initialize requests missing `MCP-Session-Id` return HTTP 400
  * two separate initialize requests produce two different session IDs
  * requests with each session ID are accepted independently
  * HTTP DELETE terminates a session and subsequent requests for that session return HTTP 404

## Step-by-Step Instructions
1. Add a new conformance section (or a new TEST_CASE) configuring:
   - `mcp::transport::http::StreamableHttpServerOptions options;`
   - `options.http.requireSessionId = true;`
2. Install a request handler that returns a valid `InitializeResult` for initialize requests.
   - Use the existing helpers in the file that construct initialize responses.
3. Issue initialize request A (POST, no `MCP-Session-Id`) and assert:
   - HTTP 200
   - response has `MCP-Session-Id`
4. Issue initialize request B (POST, no `MCP-Session-Id`) and assert:
   - HTTP 200
   - response has a different `MCP-Session-Id`
5. For each session ID, send `notifications/initialized` and assert HTTP 202.
6. Send a non-initialize request/notification without `MCP-Session-Id` and assert HTTP 400.
7. Send HTTP DELETE with session A and assert HTTP 204, then GET/POST with session A and assert HTTP 404.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_conformance_streamable_http_transport_test --output-on-failure`
