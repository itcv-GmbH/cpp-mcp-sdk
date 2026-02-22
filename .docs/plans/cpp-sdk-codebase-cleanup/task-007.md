# Task ID: task-007
# Task Name: Split `include/mcp/util/tasks.hpp`

## Context
This task is responsible for converting `include/mcp/util/tasks.hpp` into an umbrella header and introducing per-type headers for all public `class` and `struct` types in the tasks module.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules; Required Header Splits)
*   `include/mcp/util/tasks.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Output / Definition of Done
*   `include/mcp/util/tasks.hpp` contains zero `class` declarations and zero `struct` declarations.
*   `include/mcp/util/tasks.hpp` remains available at its current include path and re-exports the required types by including per-type headers.
*   Per-type headers exist for all top-level `class` and `struct` types formerly defined in `include/mcp/util/tasks.hpp`, including at minimum:
    *   `mcp::util::TaskStore`
    *   `mcp::util::InMemoryTaskStore`
    *   `mcp::util::TaskReceiver`
*   `tools/checks/check_public_header_one_type.py` reports zero violations for the tasks module headers.

The per-type header set is required to cover the following top-level `class` and `struct` types currently defined in `include/mcp/util/tasks.hpp`:
*   `Task`
*   `CreateTaskResult`
*   `TaskAugmentationRequest`
*   `TaskRecordResult`
*   `TaskTerminalResult`
*   `TaskCreateOptions`
*   `InMemoryTaskStoreOptions`
*   `TaskStore`
*   `InMemoryTaskStore`
*   `TaskReceiver`

## Step-by-Step Instructions
1.  Create per-type headers under `include/mcp/util/` for each top-level `class` and `struct` defined in `include/mcp/util/tasks.hpp` using `snake_case` basenames.
2.  Move each type declaration into its corresponding per-type header without changing declarations.
3.  Update `include/mcp/util/tasks.hpp` to include the per-type headers and to remove all `class` and `struct` declarations.
4.  Build and run unit tests that cover tasks behavior.
5.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
