# Task ID: task-018
# Task Name: Split `include/mcp/client/elicitation.hpp`

## Context
This task is responsible for converting `include/mcp/client/elicitation.hpp` into an umbrella header and introducing per-type headers for elicitation-related public `struct` types.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/client/elicitation.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Output / Definition of Done
*   `include/mcp/client/elicitation.hpp` contains zero `class` declarations and zero `struct` declarations.
*   Per-type headers exist under `include/mcp/client/` for all top-level `struct` types formerly defined in `include/mcp/client/elicitation.hpp`:
    *   `ElicitationCreateContext`
    *   `FormElicitationRequest`
    *   `UrlElicitationRequest`
    *   `UrlElicitationDisplayInfo`
    *   `FormElicitationResult`
    *   `UrlElicitationResult`
    *   `UrlElicitationRequiredItem`
    *   `UrlElicitationRequiredErrorData`
*   `tools/checks/check_public_header_one_type.py` reports zero violations for elicitation headers.

## Step-by-Step Instructions
1.  Create one per-type header under `include/mcp/client/` for each listed type using `snake_case` basenames.
2.  Move each type declaration into its corresponding per-type header without changing declarations.
3.  Update `include/mcp/client/elicitation.hpp` to include the per-type headers and to remove all `class` and `struct` declarations.
4.  Build and run unit tests that cover elicitation behavior.
5.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
