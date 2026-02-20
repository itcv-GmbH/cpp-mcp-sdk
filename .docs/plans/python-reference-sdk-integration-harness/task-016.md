# Task ID: task-016
# Task Name: Add C++ Client To Python Server Coverage Tests

## Context
This task will add Python scripts that orchestrate the expanded Python reference server fixture and execute the new C++ client fixture executables, producing automated CTest coverage for the remaining protocol surface.

## Inputs
* `tests/integration/scripts/cpp_client_to_reference_server.py` (Existing orchestrator)
* `tests/integration/fixtures/reference_python_server.py` (Expanded)
* New C++ client fixtures from `task-015`
* `tests/integration/CMakeLists.txt`
* `tests/integration/COVERAGE.md`

## Output / Definition of Done
* `tests/integration/scripts/cpp_client_to_reference_server_utilities.py` exists and runs `mcp_sdk_test_integration_cpp_client_utilities` against the reference server.
* `tests/integration/scripts/cpp_client_to_reference_server_resources_advanced.py` exists and runs `mcp_sdk_test_integration_cpp_client_resources_advanced`.
* `tests/integration/scripts/cpp_client_to_reference_server_roots.py` exists and runs `mcp_sdk_test_integration_cpp_client_roots`.
* `tests/integration/scripts/cpp_client_to_reference_server_tasks.py` exists and runs `mcp_sdk_test_integration_cpp_client_tasks`.
* `tests/integration/CMakeLists.txt` registers each script as an `integration_reference` CTest.
* `tests/integration/COVERAGE.md` maps these tests to their protocol surface items.

## Step-by-Step Instructions
1. Clone the process orchestration logic from `tests/integration/scripts/cpp_client_to_reference_server.py`.
2. Ensure each script starts the Python reference server fixture, waits for readiness, runs the C++ client fixture, and terminates the server.
3. Ensure each script validates unauthenticated behavior when the SRS requires authorization semantics.
4. Update `tests/integration/CMakeLists.txt` and `tests/integration/COVERAGE.md`.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_integration_reference_cpp_client_to_reference_server_ --output-on-failure`
