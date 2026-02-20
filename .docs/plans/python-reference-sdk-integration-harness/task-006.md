# Task ID: task-006
# Task Name: Implement C++ HTTP Roots Fixture

## Context
Roots are a client feature. This task will add a C++ Streamable HTTP server fixture that issues `roots/list` requests to the Python reference client and validates the response and `notifications/roots/list_changed` behavior.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Request: `roots/list`, Notification: `notifications/roots/list_changed`)
* `.docs/requirements/mcp-spec-2025-11-25/spec/client/roots.md`
* Existing fixture: `tests/integration/cpp_server_fixture.cpp`
* Python harness: outputs from `task-002`

## Output / Definition of Done
* `tests/integration/cpp_server_roots_fixture.cpp` exists and builds into `mcp_sdk_test_integration_cpp_server_roots`.
* The fixture validates that the Python reference client responds to `roots/list` with at least one root.
* The fixture validates that the Python reference client emits `notifications/roots/list_changed` when roots are mutated by the client test script.

## Step-by-Step Instructions
1. Create `tests/integration/cpp_server_roots_fixture.cpp` based on `tests/integration/cpp_server_fixture.cpp`.
2. After the session is initialized, send a `roots/list` request to the connected client and validate the response schema.
3. Implement a test-controlled synchronization mechanism in the fixture (stdout markers and an inbound tool call) that instructs the client script to mutate roots.
4. Capture `notifications/roots/list_changed` emitted by the client and validate the notification schema.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
* `cmake --build build/vcpkg-unix-release`
