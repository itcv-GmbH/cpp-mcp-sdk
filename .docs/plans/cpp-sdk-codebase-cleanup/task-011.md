# Task ID: task-011
# Task Name: Split `include/mcp/auth/protected_resource_metadata.hpp`

## Context
This task is responsible for converting `include/mcp/auth/protected_resource_metadata.hpp` into an umbrella header and introducing per-type headers for its public `class` and `struct` types.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/auth/protected_resource_metadata.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Output / Definition of Done
*   `include/mcp/auth/protected_resource_metadata.hpp` contains zero `class` declarations and zero `struct` declarations.
*   Per-type headers exist for all top-level `class` and `struct` types formerly defined in `include/mcp/auth/protected_resource_metadata.hpp`.
*   `tools/checks/check_public_header_one_type.py` reports zero violations for the protected resource metadata module headers.

## Step-by-Step Instructions
1.  Create per-type headers under `include/mcp/auth/` for each top-level `class` and `struct` defined in `include/mcp/auth/protected_resource_metadata.hpp` using `snake_case` basenames.
2.  Move each type declaration into its corresponding per-type header without changing declarations.
3.  Update `include/mcp/auth/protected_resource_metadata.hpp` to include the per-type headers and to remove all `class` and `struct` declarations.
4.  Build and run unit tests that cover authorization discovery behavior.
5.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
