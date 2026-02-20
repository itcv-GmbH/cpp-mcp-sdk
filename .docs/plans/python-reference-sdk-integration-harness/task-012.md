# Task ID: task-012
# Task Name: Add Python Client HTTP Coverage Tests

## Context
This task will add Python scripts that run the official Python reference client against the new C++ Streamable HTTP fixtures and validate full protocol coverage for utilities, advanced resources, roots, and tasks.

## Inputs
* `tests/integration/CMakeLists.txt`
* Python harness: `tests/integration/python/*` from `task-002`
* C++ fixtures from `task-004` through `task-007`
* `.docs/requirements/cpp-mcp-sdk.md` (Full protocol surface list)

## Output / Definition of Done
* `tests/integration/scripts/reference_client_to_cpp_server_utilities.py` exists and validates:
  - `ping`
  - `logging/setLevel`
  - `completion/complete`
  - `notifications/message`
* `tests/integration/scripts/reference_client_to_cpp_server_resources_advanced.py` exists and validates:
  - `resources/templates/list`
  - `resources/subscribe`
  - `resources/unsubscribe`
  - `notifications/resources/updated`
  - `notifications/resources/list_changed`
* `tests/integration/scripts/reference_client_to_cpp_server_roots.py` exists and validates:
  - `roots/list`
  - `notifications/roots/list_changed`
* `tests/integration/scripts/reference_client_to_cpp_server_tasks.py` exists and validates:
  - `tasks/list`
  - `tasks/get`
  - `tasks/result`
  - `tasks/cancel`
  - `notifications/tasks/status`
  - `notifications/progress`
  - `notifications/cancelled`
* `tests/integration/CMakeLists.txt` registers each script as an `integration_reference` CTest.
* `tests/integration/COVERAGE.md` maps the above scripts to the protocol surface items they cover.

## Step-by-Step Instructions
1. Implement each script using the pinned venv interpreter and the shared raw harness from `tests/integration/python/*`.
2. For each script, start the corresponding C++ fixture process, wait for readiness, and then execute assertions.
3. Each script must capture and validate expected notifications using the raw harness notification queue.
4. Each script must terminate the fixture process and must fail fast on timeouts.
5. Update `tests/integration/CMakeLists.txt` and `tests/integration/COVERAGE.md`.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_integration_reference_client_to_cpp_server_ --output-on-failure`
