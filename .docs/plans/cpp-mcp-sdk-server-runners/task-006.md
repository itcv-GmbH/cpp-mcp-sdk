# Task ID: task-006
# Task Name: Add HTTP Runner Unit Tests

## Context
The HTTP runner must preserve Streamable HTTP semantics and maintain per-session lifecycle isolation.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP + session requirements)
* `include/mcp/server/streamable_http_runner.hpp`
* `tests/conformance/test_streamable_http_transport.cpp` (existing conformance patterns)

## Output / Definition of Done
* `tests/server_streamable_http_runner_test.cpp` added.
* `tests/CMakeLists.txt` updated to build/register `mcp_sdk_server_streamable_http_runner_test`.
* Tests cover:
  * two independent sessions can both initialize without interfering (server factory invoked twice)
  * server-initiated outbound message uses `enqueueServerMessage` with sessionId (can be validated by observing StreamableHttpServer output for that session)

## Step-by-Step Instructions
1. Create a `ServerFactory` that increments an atomic counter and returns a server configured with minimal capabilities.
2. Instantiate runner with localhost bind + ephemeral port.
3. Drive the runner using `transport::http::ServerRequest` objects (avoid network flakiness by calling the underlying handler directly if runner exposes it, or by using `HttpClientRuntime` against `localPort()` if stable).
4. Simulate two sessions:
   - POST initialize without session header; capture `MCP-Session-Id` from response
   - repeat to obtain a second sessionId
5. Assert server factory count == 2 and both sessions reach operating state after `notifications/initialized`.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_server_streamable_http_runner_test --output-on-failure`
