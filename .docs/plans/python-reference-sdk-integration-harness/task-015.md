# Task ID: task-015
# Task Name: Add C++ Client Fixtures For Full Surface

## Context
This task will add C++ client fixture executables that exercise the remaining MCP protocol surface against the expanded Python reference server fixture.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Full protocol surface list)
* `tests/integration/cpp_client_fixture.cpp` (Existing client fixture)
* `tests/integration/fixtures/reference_python_server.py` (Expanded by `task-014`)

## Output / Definition of Done
* `tests/integration/cpp_client_utilities_fixture.cpp` exists and validates client-side interop for `ping`, `logging/setLevel`, and `completion/complete`.
* `tests/integration/cpp_client_resources_advanced_fixture.cpp` exists and validates templates and subscriptions and validates `notifications/resources/updated` and `notifications/resources/list_changed`.
* `tests/integration/cpp_client_roots_fixture.cpp` exists and validates `roots/list` and `notifications/roots/list_changed` in the direction required by the roots specification.
* `tests/integration/cpp_client_tasks_fixture.cpp` exists and validates tasks methods, progress, and cancellation.

## Step-by-Step Instructions
1. Create each new C++ client fixture by cloning the connection and authorization setup in `tests/integration/cpp_client_fixture.cpp`.
2. Implement raw JSON-RPC calls for methods that do not have high-level C++ client convenience wrappers.
3. Implement notification capture in the C++ client fixtures and assert required sequences.
4. Update `tests/integration/CMakeLists.txt` to build the new client fixture executables.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
* `cmake --build build/vcpkg-unix-release`
