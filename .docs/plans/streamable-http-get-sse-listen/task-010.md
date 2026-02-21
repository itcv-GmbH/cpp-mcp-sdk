# Task ID: task-010
# Task Name: Implement Session Expiration Handling For HTTP 404

## Context

The MCP Streamable HTTP specification requires that when a server terminates a session it will respond with HTTP 404 for subsequent requests using that session ID, and the client is responsible for starting a new session by re-running initialization without a session ID. The SDK must enforce correct session behavior by clearing cached HTTP session header state on HTTP 404 and by ensuring further HTTP requests do not continue to present a terminated session ID.

## Inputs

* `.docs/requirements/cpp-mcp-sdk.md` section: "Streamable HTTP Session Management"
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md` section: "Session Management"
* `include/mcp/transport/http.hpp` (`SessionHeaderState`, `ProtocolVersionHeaderState`, shared header state from `task-008`)
* `src/transport/http_client.cpp` (status handling for POST and GET listen requests)
* `src/transport/streamable_http_client_transport.cpp` (listen loop integration from `task-004`)

## Output / Definition of Done

* The Streamable HTTP client implementation will clear `MCP-Session-Id` state immediately after receiving HTTP 404 for any request that includes an `MCP-Session-Id`.
* The GET listen loop will terminate when HTTP 404 is encountered.
* Further HTTP requests issued by the same `mcp::Client` instance will not include `MCP-Session-Id` until a new successful `initialize` captures a new session ID.
* A unit test will validate that after a simulated HTTP 404:
  - the next outgoing request does not contain `MCP-Session-Id`
  - the listen loop does not continue polling with the expired session

## Step-by-Step Instructions

1. Update the Streamable HTTP client request execution path so that HTTP 404 responses are detected and mapped to a session-expired state.
2. Clear the shared session header state and clear any GET listen stream cursor state maintained by the Streamable HTTP client.
3. Ensure that the client transport surfaces an explicit error when a request fails due to HTTP 404 so that callers will run a new initialization lifecycle.
4. Update or add a unit test that uses a stubbed `RequestExecutor` to:
   - return an `InitializeResult` that sets `MCP-Session-Id`
   - return HTTP 404 for a subsequent request using that session
   - assert that the next request headers omit `MCP-Session-Id`
5. Update the GET listen tests to validate that polling terminates on HTTP 404.

## Verification

* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R transport_http_client -V`
