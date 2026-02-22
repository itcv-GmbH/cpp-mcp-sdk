# Task ID: task-022
# Task Name: Split `include/mcp/server/resources.hpp`

## Context
This task is responsible for converting `include/mcp/server/resources.hpp` into an umbrella header and introducing per-type headers for server resource model types.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/server/resources.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Output / Definition of Done
*   `include/mcp/server/resources.hpp` contains zero `class` declarations and zero `struct` declarations.
*   Per-type headers exist under `include/mcp/server/` for the top-level `class` and `struct` types formerly defined in `include/mcp/server/resources.hpp`:
    *   `ResourceDefinition`
    *   `ResourceTemplateDefinition`
    *   `ResourceContent`
    *   `ResourceReadContext`
    *   `RegisteredResource`
    *   `ResourceSubscription`
*   `tools/checks/check_public_header_one_type.py` reports zero violations for server resources headers.

## Step-by-Step Instructions
1.  Create one per-type header under `include/mcp/server/` for each listed type using `snake_case` basenames.
2.  Move each type declaration into its corresponding per-type header without changing declarations.
3.  Update `include/mcp/server/resources.hpp` to include the per-type headers and to remove all `class` and `struct` declarations.
4.  Build and run unit tests that cover server resources behavior.
5.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
