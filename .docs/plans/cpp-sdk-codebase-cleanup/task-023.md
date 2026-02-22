# Task ID: task-023
# Task Name: Split `include/mcp/server/prompts.hpp`

## Context
This task is responsible for converting `include/mcp/server/prompts.hpp` into an umbrella header and introducing per-type headers for server prompt model types.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/server/prompts.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Dependencies
*   This task depends on: `task-001`, `task-002`, `task-003`, `task-004`.

## Output / Definition of Done
*   `include/mcp/server/prompts.hpp` contains zero `class` declarations and zero `struct` declarations.
*   Per-type headers exist under `include/mcp/server/` for the top-level `class` and `struct` types formerly defined in `include/mcp/server/prompts.hpp`:
    *   `PromptArgumentDefinition`
    *   `PromptDefinition`
    *   `PromptMessage`
    *   `PromptGetResult`
    *   `PromptGetContext`
    *   `RegisteredPrompt`
*   `tools/checks/check_public_header_one_type.py` reports zero violations for server prompts headers.

## Step-by-Step Instructions
1.  Create one per-type header under `include/mcp/server/` for each listed type using `snake_case` basenames.
2.  Move each type declaration into its corresponding per-type header without changing declarations.
3.  Update `include/mcp/server/prompts.hpp` to include the per-type headers and to remove all `class` and `struct` declarations.
4.  Build and run unit tests that cover server prompts behavior.
5.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
