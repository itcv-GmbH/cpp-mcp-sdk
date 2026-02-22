# Task ID: task-016
# Task Name: Split `include/mcp/jsonrpc/router.hpp`

## Context
This task is responsible for converting `include/mcp/jsonrpc/router.hpp` into an umbrella header and introducing per-type headers for router-related public types.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/jsonrpc/router.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Output / Definition of Done
*   `include/mcp/jsonrpc/router.hpp` contains zero `class` declarations and zero `struct` declarations.
*   Per-type headers exist under `include/mcp/jsonrpc/` for all top-level `class` and `struct` types formerly defined in `include/mcp/jsonrpc/router.hpp`:
    *   `RouterOptions`
    *   `ProgressUpdate`
    *   `OutboundRequestOptions`
    *   `Router`
*   `tools/checks/check_public_header_one_type.py` reports zero violations for the jsonrpc router module headers.

## Step-by-Step Instructions
1.  Create one per-type header under `include/mcp/jsonrpc/` for each listed type using `snake_case` basenames.
2.  Move each type declaration into its corresponding per-type header without changing declarations.
3.  Update `include/mcp/jsonrpc/router.hpp` to include the per-type headers and to remove all `class` and `struct` declarations.
4.  Update `src/jsonrpc/router.cpp` includes to align with the split while preserving `<mcp/jsonrpc/router.hpp>` include stability.
5.  Build and run unit tests that cover router behavior.
6.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
