# Task ID: task-007
# Task Name: Implement C++ HTTP Tasks Fixture

## Context
This task will add a C++ Streamable HTTP server fixture that exercises the MCP tasks utility against the Python reference client, including task lifecycle notifications, progress reporting, and cancellation.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Requests: `tasks/get`, `tasks/list`, `tasks/result`, `tasks/cancel`)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/tasks.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/progress.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/cancellation.md`
* Python harness: outputs from `task-002`

## Output / Definition of Done
* `tests/integration/cpp_server_tasks_fixture.cpp` exists and builds into `mcp_sdk_test_integration_cpp_server_tasks`.
* The fixture issues tasks-related requests and validates responses:
  - `tasks/list`
  - `tasks/get`
  - `tasks/result`
  - `tasks/cancel`
* The fixture emits and validates notifications:
  - `notifications/tasks/status`
  - `notifications/progress`
  - `notifications/cancelled`
* The fixture validates `params.task` augmentation behavior required by the tasks specification.

## Step-by-Step Instructions
1. Create `tests/integration/cpp_server_tasks_fixture.cpp` as a Streamable HTTP fixture.
2. Implement a deterministic long-running task in the fixture with a stable task id and stable status transitions.
3. Emit `notifications/tasks/status` and `notifications/progress` for the task at deterministic checkpoints.
4. Handle `tasks/cancel` and emit `notifications/cancelled` and a final `notifications/tasks/status` transition.
5. Provide a deterministic `tasks/result` value for completed tasks and a deterministic error for cancelled tasks.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
* `cmake --build build/vcpkg-unix-release`
