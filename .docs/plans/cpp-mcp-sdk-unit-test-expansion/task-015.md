# Task ID: [task-015]
# Task Name: [Expand Unit Tests: Streamable HTTP Server]

## Context
Increase Streamable HTTP server unit tests for request validation, SSE priming/resume behavior, and session/version header handling.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP; session management)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
* `include/mcp/transport/http.hpp`
* `src/transport/http_server.cpp`
* `tests/transport_http_server_test.cpp`

## Output / Definition of Done
* `tests/transport_http_server_test.cpp` adds tests for:
  - invalid/missing `Content-Type` for POST bodies -> 400
  - POST with batch/concatenated JSON -> 400
  - SSE priming event emitted at stream open (id present, empty data)
  - Last-Event-ID resume rejects cross-stream IDs and stale buffer IDs with correct status

## Step-by-Step Instructions
1. Add tests for invalid content-type and invalid JSON bodies.
2. Add SSE priming assertion for GET stream open.
3. Add resume tests exercising bad `Last-Event-ID` formats.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_transport_http_server_test --output-on-failure`
