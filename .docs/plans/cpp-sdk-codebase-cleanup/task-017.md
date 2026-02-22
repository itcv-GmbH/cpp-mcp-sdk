# Task ID: task-017
# Task Name: Split `include/mcp/client/client.hpp`

## Context
This task is responsible for converting `include/mcp/client/client.hpp` into an umbrella header and introducing per-type headers for client-related public `class` and `struct` types.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/client/client.hpp`
*   `src/client/client.cpp`
*   `tools/checks/check_public_header_one_type.py`

## Dependencies
*   This task depends on: `task-001`, `task-002`, `task-003`, `task-004`, `task-014`, `task-015`, `task-016`.

## Output / Definition of Done
*   `include/mcp/client/client.hpp` contains zero `class` declarations and zero `struct` declarations.
*   Per-type headers exist under `include/mcp/client/` for all top-level `class` and `struct` types formerly defined in `include/mcp/client/client.hpp`:
    *   `ClientInitializeConfiguration`
    *   `ListToolsResult`
    *   `ListResourcesResult`
    *   `ReadResourceResult`
    *   `ListResourceTemplatesResult`
    *   `ListPromptsResult`
    *   `Client`
*   `tools/checks/check_public_header_one_type.py` reports zero violations for the client module headers.

## Step-by-Step Instructions
1.  Create one per-type header under `include/mcp/client/` for each listed type using `snake_case` basenames.
2.  Move each type declaration into its corresponding per-type header without changing declarations.
3.  Update `include/mcp/client/client.hpp` to include the per-type headers and to remove all `class` and `struct` declarations.
4.  Update `src/client/client.cpp` include list to remain valid after the header split.
5.  Build and run unit tests that cover client behavior.
6.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
