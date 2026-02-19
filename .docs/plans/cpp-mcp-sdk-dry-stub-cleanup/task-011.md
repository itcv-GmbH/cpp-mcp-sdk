# Task ID: task-011
# Task Name: Session Lifecycle API De-Slop

## Context
`mcp::Session::sendRequest()` contains TODOs implying it performs transport-backed sending, but the SDK currently uses it for lifecycle gating only (Client/Server send via `jsonrpc::Router`). This task removes misleading TODOs, clarifies responsibility boundaries, and updates tests so they don’t rely on placeholder behavior.

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Lifecycle management; bidirectional operation)
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/lifecycle.md`
*   `include/mcp/lifecycle/session.hpp`
*   `src/lifecycle/session.cpp`
*   Call sites in `src/client/client.cpp` and `src/server/server.cpp`
*   Tests in `tests/lifecycle_test.cpp`

## Output / Definition of Done
*   `src/lifecycle/session.cpp` no longer contains TODOs suggesting `Session` performs I/O for requests.
*   New explicit lifecycle-enforcement API added (example):
    *   `Session::onOutboundRequest(std::string_view method, const jsonrpc::JsonValue &params, RequestOptions options) -> void`
*   `src/client/client.cpp` and `src/server/server.cpp` updated to call the lifecycle-enforcement method (instead of calling `Session::sendRequest()` and discarding a placeholder future).
*   `tests/lifecycle_test.cpp` updated to exercise lifecycle enforcement via the new API (and avoid asserting against placeholder transport behavior).

## Step-by-Step Instructions
1.  Introduce a new `Session` method whose name communicates lifecycle enforcement (not transport I/O). Prefer accepting the minimal data needed (method + params).
2.  Refactor `Session::sendRequest()` to either:
    *   (preferred) become a thin deprecated wrapper that calls the new enforcement method and returns a ready future containing a clear error response, or
    *   be removed if the SDK’s API policy allows a breaking change.
3.  Remove/resolve the `Session::stop()` TODO by either implementing transport shutdown behavior or clarifying that transport shutdown is managed by `Client`/`Server`.
4.  Update `src/client/client.cpp` to use the new enforcement method in `Client::sendRequest()`.
5.  Update server lifecycle gating call sites similarly (where applicable).
6.  Update `tests/lifecycle_test.cpp`:
    *   replace calls that depended on “sendRequest placeholder future” behavior
    *   add a test that ensures lifecycle enforcement updates session state correctly on initialize

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_lifecycle_test --output-on-failure`
*   `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_conformance_lifecycle_test --output-on-failure`
