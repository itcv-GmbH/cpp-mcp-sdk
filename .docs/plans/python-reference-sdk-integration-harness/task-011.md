# Task ID: task-011
# Task Name: Implement C++ STDIO Tasks Fixture

## Context
This task will add a C++ STDIO server fixture that exercises the MCP tasks utility against the Python reference client over STDIO.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Requests: `tasks/get`, `tasks/list`, `tasks/result`, `tasks/cancel`)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/tasks.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/progress.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/cancellation.md`
* Existing fixture: `tests/integration/cpp_stdio_server_fixture.cpp`
* Python harness: outputs from `task-002`

## Output / Definition of Done
* `tests/integration/cpp_stdio_server_tasks_fixture.cpp` exists and builds into `mcp_sdk_test_integration_cpp_stdio_server_tasks`.
* The fixture implements tasks requests and emits tasks and progress notifications.

## Step-by-Step Instructions
1. Create `tests/integration/cpp_stdio_server_tasks_fixture.cpp`.
2. Implement a deterministic long-running task and expose it via tasks methods.
3. Emit `notifications/tasks/status`, `notifications/progress`, and `notifications/cancelled`.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
* `cmake --build build/vcpkg-unix-release`
