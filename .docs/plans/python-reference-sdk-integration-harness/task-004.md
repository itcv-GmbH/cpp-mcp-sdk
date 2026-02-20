# Task ID: task-004
# Task Name: Implement C++ HTTP Utilities Fixture

## Context
This task will add a dedicated C++ Streamable HTTP fixture that exposes and exercises MCP utilities and utility-related notifications against the Python reference client.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Requests: `ping`, `logging/setLevel`, `completion/complete`)
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/logging.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/completion.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/ping.md`
* Existing fixture: `tests/integration/cpp_server_fixture.cpp`
* Python harness: outputs from `task-002`

## Output / Definition of Done
* `tests/integration/cpp_server_utilities_fixture.cpp` exists and builds into `mcp_sdk_test_integration_cpp_server_utilities`.
* The fixture implements handlers for:
  - `ping`
  - `logging/setLevel`
  - `completion/complete`
* The fixture emits `notifications/message` after `logging/setLevel` to validate client notification handling.
* The fixture prints a readiness line to stdout that includes the final endpoint URL.

## Step-by-Step Instructions
1. Create `tests/integration/cpp_server_utilities_fixture.cpp` by cloning the process model in `tests/integration/cpp_server_fixture.cpp`.
2. Configure Streamable HTTP runner with bearer-token verification identical to the existing fixture.
3. Register MCP handlers on the server for `ping`, `logging/setLevel`, and `completion/complete` according to the spec mirror.
4. Implement a deterministic trigger that causes the server to emit `notifications/message` after the client sets log level.
5. Ensure the fixture terminates cleanly when stdin is closed.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_test_integration_cpp_server_utilities --output-on-failure`
