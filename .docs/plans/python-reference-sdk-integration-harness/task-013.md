# Task ID: task-013
# Task Name: Add Python Client STDIO Coverage Tests

## Context
This task will add Python scripts that run the official Python reference client against the new C++ STDIO fixtures and validate full protocol coverage for utilities, advanced resources, roots, and tasks over STDIO.

## Inputs
* `tests/integration/CMakeLists.txt`
* Python harness: `tests/integration/python/*` from `task-002`
* C++ STDIO fixtures from `task-008` through `task-011`
* `.docs/requirements/cpp-mcp-sdk.md` (Full protocol surface list)

## Output / Definition of Done
* `tests/integration/scripts/reference_client_to_cpp_stdio_server_utilities.py` exists and validates `ping`, `logging/setLevel`, `completion/complete`, and `notifications/message` over STDIO.
* `tests/integration/scripts/reference_client_to_cpp_stdio_server_resources_advanced.py` exists and validates templates and subscriptions over STDIO.
* `tests/integration/scripts/reference_client_to_cpp_stdio_server_roots.py` exists and validates roots over STDIO.
* `tests/integration/scripts/reference_client_to_cpp_stdio_server_tasks.py` exists and validates tasks, progress, and cancellation over STDIO.
* `tests/integration/CMakeLists.txt` registers each script as an `integration_reference` CTest.
* `tests/integration/COVERAGE.md` maps these scripts to their protocol surface items.

## Step-by-Step Instructions
1. Implement each script using the STDIO raw harness from `tests/integration/python/stdio_raw.py`.
2. Each script must validate notifications via the harness notification queue.
3. Each script must enforce deterministic timeouts and deterministic teardown.
4. Update `tests/integration/CMakeLists.txt` and `tests/integration/COVERAGE.md`.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_integration_reference_client_to_cpp_stdio_server_ --output-on-failure`
