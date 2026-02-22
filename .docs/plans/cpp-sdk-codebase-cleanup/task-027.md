# Task ID: task-027
# Task Name: Split `include/mcp/http/sse.hpp`

## Context
This task is responsible for converting `include/mcp/http/sse.hpp` into an umbrella header and introducing per-type headers for SSE public types.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/http/sse.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Output / Definition of Done
*   `include/mcp/http/sse.hpp` contains zero `class` declarations and zero `struct` declarations.
*   Per-type headers exist under `include/mcp/http/` for the top-level `class` and `struct` types formerly defined in `include/mcp/http/sse.hpp`:
    *   `EventIdCursor`
    *   `Event`
*   `tools/checks/check_public_header_one_type.py` reports zero violations for SSE headers.

## Step-by-Step Instructions
1.  Create one per-type header under `include/mcp/http/` for each listed type using `snake_case` basenames.
2.  Move each type declaration into its corresponding per-type header without changing declarations.
3.  Update `include/mcp/http/sse.hpp` to include the per-type headers and to remove all `class` and `struct` declarations.
4.  Build and run unit tests that cover SSE usage in the HTTP transport.
5.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
