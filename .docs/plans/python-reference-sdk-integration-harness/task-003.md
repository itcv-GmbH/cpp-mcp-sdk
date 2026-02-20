# Task ID: task-003
# Task Name: Add Integration Test Wiring For New Suites

## Context
This task will wire the expanded Python reference integration suite into CMake/CTest so the tests run automatically under the `integration_reference` label and have deterministic dependency ordering.

## Inputs
* `tests/integration/CMakeLists.txt` (Current CTest wiring)
* `tests/CMakeLists.txt` (Integration subdirectory gating by `MCP_SDK_INTEGRATION_TESTS`)
* `tests/integration/fixtures/reference_python_requirements.txt` (Pinned Python deps)
* Output files from `task-001` and `task-002`

## Output / Definition of Done
* `tests/integration/CMakeLists.txt` registers new fixtures and new Python scripts as CTest tests.
* Every integration_reference test depends on `mcp_sdk_integration_reference_setup_python_sdk`.
* Every integration_reference test has `PYTHONUNBUFFERED=1` in its environment.
* Test names are stable and unique and use the `mcp_sdk_integration_reference_*` prefix.

## Step-by-Step Instructions
1. Add `add_executable(...)` entries for the new C++ fixture executables defined by this plan.
2. Add `add_test(NAME ... COMMAND "${MCP_SDK_INTEGRATION_PYTHON_EXECUTABLE}" <script> ...)` entries for each new Python integration script.
3. Set `LABELS "integration;integration_reference"` and `TIMEOUT 300` for every integration_reference test.
4. Set `DEPENDS mcp_sdk_integration_reference_setup_python_sdk` for every integration_reference test.
5. Register the coverage gate test from `task-001` as a dependency for all other integration_reference tests.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -N | grep integration_reference`
