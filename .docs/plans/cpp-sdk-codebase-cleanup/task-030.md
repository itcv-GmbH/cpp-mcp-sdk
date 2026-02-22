# Task ID: task-030
# Task Name: Normalize `include/mcp/security/limits.hpp`, `include/mcp/util/cancellation.hpp`, `include/mcp/util/progress.hpp`, and `include/mcp/detail/url.hpp` Basenames

## Context
This task is responsible for enforcing the per-type header basename rule for detail, util, and security headers that currently define a public `class` or `struct` whose basename does not match the type name.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/security/limits.hpp`
*   `include/mcp/util/cancellation.hpp`
*   `include/mcp/util/progress.hpp`
*   `include/mcp/detail/url.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Dependencies
*   This task depends on: `task-001`, `task-002`, `task-003`, `task-004`.

## Output / Definition of Done
*   `include/mcp/security/runtime_limits.hpp` exists and defines the type currently defined in `include/mcp/security/limits.hpp`.
*   `include/mcp/security/limits.hpp` contains zero `class` declarations and zero `struct` declarations and includes `include/mcp/security/runtime_limits.hpp`.
*   `include/mcp/util/cancellation/cancelled_notification.hpp` exists and defines the type currently defined in `include/mcp/util/cancellation.hpp`.
*   `include/mcp/util/cancellation.hpp` contains zero `class` declarations and zero `struct` declarations and includes `include/mcp/util/cancellation/cancelled_notification.hpp`.
*   `include/mcp/util/progress/progress_notification.hpp` exists and defines the type currently defined in `include/mcp/util/progress.hpp`.
*   `include/mcp/util/progress.hpp` contains zero `class` declarations and zero `struct` declarations and includes `include/mcp/util/progress/progress_notification.hpp`.
*   `include/mcp/detail/parsed_absolute_url.hpp` exists and defines the type currently defined in `include/mcp/detail/url.hpp`.
*   `include/mcp/detail/url.hpp` contains zero `class` declarations and zero `struct` declarations and includes `include/mcp/detail/parsed_absolute_url.hpp`.

## Step-by-Step Instructions
1.  Create `include/mcp/security/runtime_limits.hpp` and move the type definition from `include/mcp/security/limits.hpp` into it without changing declarations.
2.  Update `include/mcp/security/limits.hpp` to include `include/mcp/security/runtime_limits.hpp` and to remove the moved `struct` definition.
3.  Create directory `include/mcp/util/cancellation/` and create `include/mcp/util/cancellation/cancelled_notification.hpp`.
4.  Move the type definition from `include/mcp/util/cancellation.hpp` into `include/mcp/util/cancellation/cancelled_notification.hpp` without changing declarations.
5.  Update `include/mcp/util/cancellation.hpp` to include the new per-type header and to remove the moved `struct` definition.
6.  Create directory `include/mcp/util/progress/` and create `include/mcp/util/progress/progress_notification.hpp`.
7.  Move the type definition from `include/mcp/util/progress.hpp` into `include/mcp/util/progress/progress_notification.hpp` without changing declarations.
8.  Update `include/mcp/util/progress.hpp` to include the new per-type header and to remove the moved `struct` definition.
9.  Create `include/mcp/detail/parsed_absolute_url.hpp` and move the type definition from `include/mcp/detail/url.hpp` into it without changing declarations.
10. Update `include/mcp/detail/url.hpp` to include `include/mcp/detail/parsed_absolute_url.hpp` and to remove the moved `struct` definition.
11. Run `tools/checks/check_public_header_one_type.py`.
12. Build and run unit tests.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
