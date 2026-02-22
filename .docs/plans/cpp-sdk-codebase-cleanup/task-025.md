# Task ID: task-025
# Task Name: Split Server Runner Headers

## Context
This task is responsible for converting server runner headers into umbrella headers and introducing per-type headers for each runner options type and runner type.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/server/combined_runner.hpp`
*   `include/mcp/server/stdio_runner.hpp`
*   `include/mcp/server/streamable_http_runner.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Output / Definition of Done
*   Each of the following headers contains zero `class` declarations and zero `struct` declarations:
    *   `include/mcp/server/combined_runner.hpp`
    *   `include/mcp/server/stdio_runner.hpp`
    *   `include/mcp/server/streamable_http_runner.hpp`
*   Per-type headers exist under `include/mcp/server/` for the runner options types and runner types:
    *   `CombinedServerRunnerOptions`, `CombinedServerRunner`
    *   `StdioServerRunnerOptions`, `StdioServerRunner`
    *   `StreamableHttpServerRunnerOptions`, `StreamableHttpServerRunner`
*   `tools/checks/check_public_header_one_type.py` reports zero violations for runner headers.

## Step-by-Step Instructions
1.  Create one per-type header under `include/mcp/server/` for each listed runner type and options type using `snake_case` basenames.
2.  Move each type declaration into its corresponding per-type header without changing declarations.
3.  Update each runner umbrella header to include its per-type headers and to remove all `class` and `struct` declarations.
4.  Build and run unit tests that cover server runner behavior.
5.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
