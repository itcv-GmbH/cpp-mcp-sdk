# Task ID: task-014
# Task Name: Implement Server-Issued MCP-Session-Id on Initialize (Transport)

## Context
The MCP Streamable HTTP spec allows (and for stateful/multi-client servers, effectively requires) the server to assign an `MCP-Session-Id` at initialization time by including it on the HTTP response containing the `InitializeResult`. The current `mcp::transport::http::StreamableHttpServer` supports validating and terminating sessions, but does not automatically issue session IDs on initialize responses. Without this, a Streamable HTTP runner cannot implement per-session `mcp::Server` isolation.

## Inputs
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md` (Session Management section)
* `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP session requirements)
* `include/mcp/transport/http.hpp` (`StreamableHttpServer`, `HttpServerOptions.requireSessionId`)
* `src/transport/http_server.cpp`
* `include/mcp/security/crypto_random.hpp` (cryptographic randomness)

## Output / Definition of Done
* `mcp::transport::http::StreamableHttpServer` issues `MCP-Session-Id` on successful initialize responses when `options.http.requireSessionId=true`.
* Session IDs are:
  * cryptographically secure
  * globally unique (practically)
  * visible ASCII only (0x21-0x7E)
* The issued session is recorded as active in the server's session table so that subsequent requests containing the session ID are accepted.
* Failed initialize responses do not leave behind an active session entry.

## Step-by-Step Instructions
1. In `src/transport/http_server.cpp`, detect initialize requests in `handlePost` (already computed as `RequestKind::kInitialize`).
2. When `options.http.requireSessionId=true` and the request is initialize:
   - generate a new session ID before calling the configured `StreamableRequestHandler`
   - set that session ID onto the request context passed to the handler (`jsonrpc::RequestContext.sessionId`)
3. After the handler returns a response:
    - if the response is a JSON-RPC success response for initialize (i.e., contains `InitializeResult`):
      - upsert the session into the internal `sessions` map as active
      - store the negotiated protocol version by reading the initialize success response JSON `result.protocolVersion` value when it is present and valid
      - include `MCP-Session-Id: <generated>` on the HTTP response headers (both JSON and SSE HTTP response cases)
   - if the initialize response is an error (or no response):
     - do not return an `MCP-Session-Id` header
     - ensure no active session entry remains for that generated ID
4. Ensure session IDs use only visible ASCII:
   - reuse the existing approach used by task IDs (hex-encoding cryptographic random bytes is acceptable and visible ASCII)
5. Ensure this interacts correctly with existing session termination:
    - HTTP DELETE with `MCP-Session-Id` must continue to mark sessions terminated and return 204
    - subsequent requests using that session must return 404

## Verification
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release`
* After `task-015` adds tests: `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_conformance_streamable_http_transport_test --output-on-failure`
