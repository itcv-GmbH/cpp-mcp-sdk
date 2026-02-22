# Task ID: task-029
# Task Name: Normalize `include/mcp/error_reporter.hpp`, `include/mcp/errors.hpp`, and `include/mcp/version.hpp` Basenames

## Context
This task is responsible for enforcing the per-type header basename rule for base headers that currently define a public `class` or `struct` whose basename does not match the type name.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/error_reporter.hpp`
*   `include/mcp/errors.hpp`
*   `include/mcp/version.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Dependencies
*   This task depends on: `task-001`, `task-002`, `task-003`, `task-004`.

## Output / Definition of Done
*   `include/mcp/error_event.hpp` exists and defines the type currently defined in `include/mcp/error_reporter.hpp`.
*   `include/mcp/error_reporter.hpp` contains zero `class` declarations and zero `struct` declarations and includes `include/mcp/error_event.hpp`.
*   `include/mcp/json_rpc_error.hpp` exists and defines the type currently defined in `include/mcp/errors.hpp`.
*   `include/mcp/errors.hpp` contains zero `class` declarations and zero `struct` declarations and includes `include/mcp/json_rpc_error.hpp`.
*   `include/mcp/negotiated_protocol_version.hpp` exists and defines the type currently defined in `include/mcp/version.hpp`.
*   `include/mcp/version.hpp` contains zero `class` declarations and zero `struct` declarations and includes `include/mcp/negotiated_protocol_version.hpp`.

## Step-by-Step Instructions
1.  Create `include/mcp/error_event.hpp` and move the type definition from `include/mcp/error_reporter.hpp` into it without changing declarations.
2.  Update `include/mcp/error_reporter.hpp` to include `include/mcp/error_event.hpp` and to remove the moved `class` definition.
3.  Create `include/mcp/json_rpc_error.hpp` and move the type definition from `include/mcp/errors.hpp` into it without changing declarations.
4.  Update `include/mcp/errors.hpp` to include `include/mcp/json_rpc_error.hpp` and to remove the moved `struct` definition.
5.  Create `include/mcp/negotiated_protocol_version.hpp` and move the type definition from `include/mcp/version.hpp` into it without changing declarations.
6.  Update `include/mcp/version.hpp` to include `include/mcp/negotiated_protocol_version.hpp` and to remove the moved `class` definition.
7.  Run `tools/checks/check_public_header_one_type.py`.
8.  Build and run unit tests.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
