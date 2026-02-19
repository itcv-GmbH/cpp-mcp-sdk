# Task ID: [task-014]
# Task Name: [Expand Unit Tests: Streamable HTTP Client]

## Context
Increase Streamable HTTP client unit tests for error handling, SSE parsing edge cases, reconnection rules, and multi-stream delivery.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP requirements)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
* `include/mcp/transport/http.hpp`
* `src/transport/http_client.cpp`
* `tests/transport_http_client_test.cpp`

## Output / Definition of Done
* `tests/transport_http_client_test.cpp` adds tests for:
  - server returns JSON with wrong content-type -> client throws actionable error
  - malformed SSE event stream -> client rejects with error
  - 404 session response triggers reinitialize path (where implemented) or surfaces actionable exception
  - notifications/responses sent as POST receive 202 and do not produce response objects

## Step-by-Step Instructions
1. Add a fixture handler that returns a 200 with `Content-Type: text/plain` and a JSON body; assert client throws.
2. Add a fixture handler that returns SSE with invalid formatting; assert client throws.
3. Add a fixture that returns 404 for session-bound requests and assert expected client behavior.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_transport_http_client_test --output-on-failure`
