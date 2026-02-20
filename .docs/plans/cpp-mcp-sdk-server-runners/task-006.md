# Task ID: task-006
# Task Name: Add HTTP Runner Unit Tests

## Context
The HTTP runner must preserve Streamable HTTP semantics and maintain per-session lifecycle isolation (one `mcp::Server` per `MCP-Session-Id`).

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP + session requirements)
* `include/mcp/server/streamable_http_runner.hpp`
* `tests/conformance/test_streamable_http_transport.cpp` (existing conformance patterns)
* `task-014` (transport issues `MCP-Session-Id` on initialize)

## Output / Definition of Done
* `tests/server_streamable_http_runner_test.cpp` added.
* Tests cover:
   * runner starts/stops cleanly on an ephemeral port
   * two sessions will both initialize without interfering (factory invoked twice)
   * server-initiated outbound message is routed via `StreamableHttpServer::enqueueServerMessage(message, sessionId)` (validated by observing SSE output)

## Step-by-Step Instructions
1. Create a `ServerFactory` that increments an atomic counter and returns a server configured with minimal capabilities.
2. Instantiate runner with localhost bind + ephemeral port and `requireSessionId=true`.
3. Start session A:
   - POST initialize with no `MCP-Session-Id`; capture `MCP-Session-Id` from HTTP response headers
   - POST `notifications/initialized` with session A; expect HTTP 202
4. Start session B:
   - POST initialize with no `MCP-Session-Id`; capture a different `MCP-Session-Id`
   - POST `notifications/initialized` with session B; expect HTTP 202
5. Assert factory count == 2.
6. Validate outbound routing:
    - open a GET SSE listen request for session A (`Accept: text/event-stream` + `MCP-Session-Id: <A>`)
    - trigger a server-initiated message on session A's `mcp::Server` by calling `server->sendNotification(mcp::jsonrpc::RequestContext{.sessionId = <A>}, notification)` (factory stores created servers in creation order; the first server instance maps to the first initialized session in this test)
    - poll GET with `Last-Event-ID` and assert the SSE payload contains the JSON-RPC message

## Verification
* After `task-010` wires the test into CMake: `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_server_streamable_http_runner_test --output-on-failure`
