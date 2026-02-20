# Task ID: task-010
# Task Name: Implement C++ STDIO Roots Fixture

## Context
This task will add a C++ STDIO server fixture that issues `roots/list` requests to the Python reference client and validates the response and `notifications/roots/list_changed` behavior over STDIO.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Request: `roots/list`, Notification: `notifications/roots/list_changed`)
* `.docs/requirements/mcp-spec-2025-11-25/spec/client/roots.md`
* Existing fixture: `tests/integration/cpp_stdio_server_fixture.cpp`
* Python harness: outputs from `task-002`

## Output / Definition of Done
* `tests/integration/cpp_stdio_server_roots_fixture.cpp` exists and builds into `mcp_sdk_test_integration_cpp_stdio_server_roots`.
* The fixture validates `roots/list` response and `notifications/roots/list_changed`.

## Step-by-Step Instructions
1. Create `tests/integration/cpp_stdio_server_roots_fixture.cpp`.
2. After initialization, send `roots/list` to the client.
3. Trigger a client-side roots change and validate reception of `notifications/roots/list_changed`.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
* `cmake --build build/vcpkg-unix-release`
