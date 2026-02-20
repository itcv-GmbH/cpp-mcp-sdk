# Task ID: task-008
# Task Name: Implement C++ STDIO Utilities Fixture

## Context
This task will add a dedicated C++ STDIO server fixture that exposes and exercises MCP utilities and utility-related notifications against the Python reference client over STDIO.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Requests: `ping`, `logging/setLevel`, `completion/complete`)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/logging.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/completion.md`
* Existing fixture: `tests/integration/cpp_stdio_server_fixture.cpp`
* Python harness: outputs from `task-002`

## Output / Definition of Done
* `tests/integration/cpp_stdio_server_utilities_fixture.cpp` exists and builds into `mcp_sdk_test_integration_cpp_stdio_server_utilities`.
* The fixture implements `ping`, `logging/setLevel`, and `completion/complete`.
* The fixture emits `notifications/message` after `logging/setLevel`.

## Step-by-Step Instructions
1. Create `tests/integration/cpp_stdio_server_utilities_fixture.cpp` based on `tests/integration/cpp_stdio_server_fixture.cpp`.
2. Register handlers for `ping`, `logging/setLevel`, and `completion/complete`.
3. Emit `notifications/message` after `logging/setLevel` and validate client receipt in the Python script.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
* `cmake --build build/vcpkg-unix-release`
