# Task ID: task-014
# Task Name: Fix StreamableHttpServer Handler Locking

## Context
`transport::http::StreamableHttpServer::handleRequest()` currently holds an internal mutex while processing requests, which includes invoking user handlers. This can deadlock if handlers call back into the server (e.g., enqueueing messages). The SRS requires reliability and production usability.

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Performance and Reliability; Streamable HTTP)
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
*   `src/transport/http_server.cpp` (`StreamableHttpServer` implementation)
*   `include/mcp/transport/http.hpp` (handler type definitions)
*   Existing HTTP tests: `tests/transport_http_common_test.cpp`, `tests/conformance/test_streamable_http_transport.cpp`

## Output / Definition of Done
*   `src/transport/http_server.cpp` refactored so internal locks are not held while invoking:
    *   `StreamableRequestHandler`
    *   `StreamableNotificationHandler`
    *   `StreamableResponseHandler`
*   Regression test added to prove no deadlock when a handler calls `StreamableHttpServer::enqueueServerMessage()`.
*   Streamable HTTP conformance tests remain green.

## Step-by-Step Instructions
1.  Refactor `StreamableHttpServer::handleRequest()` / `Impl::handleRequest()` so handler invocations happen outside the mutex:
    *   copy the handler `std::function` and any required immutable state under lock
    *   release lock
    *   invoke handler
    *   re-acquire lock only to mutate shared structures (streams, pending message buffers, session map)
2.  Add a regression test in `tests/transport_http_common_test.cpp`:
    *   set a handler that, during request handling, calls `server.enqueueServerMessage(...)`
    *   run `server.handleRequest(...)` via `std::async` and fail the test if it does not complete within a short timeout
3.  Ensure the fix does not introduce message ordering regressions (pre-response messages must still be emitted before the response where required).

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_transport_http_common_test --output-on-failure`
*   `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_conformance_streamable_http_transport_test --output-on-failure`
