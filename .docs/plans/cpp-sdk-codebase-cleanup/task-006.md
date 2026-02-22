# Task ID: task-006
# Task Name: Split `include/mcp/transport/stdio.hpp`

## Context
This task is responsible for converting `include/mcp/transport/stdio.hpp` into an umbrella header and introducing per-type headers for its public `class` and `struct` types.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/transport/stdio.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Output / Definition of Done
*   `include/mcp/transport/stdio.hpp` contains zero `class` declarations and zero `struct` declarations.
*   Per-type headers exist for the top-level `class` and `struct` types formerly defined in `include/mcp/transport/stdio.hpp`:
    *   `StdioServerOptions`
    *   `StdioClientOptions`
    *   `StdioSubprocessSpawnOptions`
    *   `StdioSubprocessShutdownOptions`
    *   `StdioSubprocess`
    *   `StdioAttachOptions`
    *   `StdioTransport`
*   `tools/checks/check_public_header_one_type.py` reports zero violations for the `stdio` module headers.

## Step-by-Step Instructions
1.  Create per-type headers under `include/mcp/transport/` for each listed type using `snake_case` basenames.
2.  Move each type declaration into its corresponding per-type header without changing declarations.
3.  Update `include/mcp/transport/stdio.hpp` to include the per-type headers and to remove all `class` and `struct` declarations.
4.  Build and run unit tests that include `<mcp/transport/stdio.hpp>`.
5.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
