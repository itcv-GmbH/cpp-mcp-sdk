# Task ID: task-013
# Task Name: Split `include/mcp/auth/provider.hpp`

## Context
This task is responsible for converting `include/mcp/auth/provider.hpp` into an umbrella header and introducing per-type headers for its public `class` and `struct` types.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/auth/provider.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Dependencies
*   This task depends on: `task-001`, `task-002`, `task-003`, `task-004`.

## Output / Definition of Done
*   `include/mcp/auth/provider.hpp` contains zero `class` declarations and zero `struct` declarations.
*   Per-type headers exist for the top-level `class` and `struct` types formerly defined in `include/mcp/auth/provider.hpp`:
    *   `AuthRequestContext`
    *   `AuthResult`
    *   `AuthProvider`
    *   `AuthVerifier`
*   `tools/checks/check_public_header_one_type.py` reports zero violations for the auth provider module headers.

## Step-by-Step Instructions
1.  Create per-type headers under `include/mcp/auth/` for each listed type using `snake_case` basenames.
2.  Move each type declaration into its corresponding per-type header without changing declarations.
3.  Update `include/mcp/auth/provider.hpp` to include the per-type headers and to remove all `class` and `struct` declarations.
4.  Build and run unit tests that cover auth provider wiring.
5.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
