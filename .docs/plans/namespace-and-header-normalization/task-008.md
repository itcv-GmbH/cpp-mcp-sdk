# Task ID: task-008
# Task Name: Normalize Util Module Umbrella Headers

## Context
The util module exposes an umbrella header at `include/mcp/util/tasks.hpp`, which violates the umbrella header policy.

This task will replace it with `include/mcp/util/all.hpp`.

## Inputs
* `include/mcp/util/tasks.hpp`
* `include/mcp/util/*.hpp`
* SRS: Functional Requirements 4

## Output / Definition of Done
* `include/mcp/util/all.hpp` is required to exist and is required to contain only `#include` directives
* `include/mcp/util/tasks.hpp` must not exist
* All includes of `<mcp/util/tasks.hpp>` are required to be replaced with `<mcp/util/all.hpp>` or per-type headers

## Step-by-Step Instructions
1. Create `include/mcp/util/all.hpp` and ensure it contains only `#include` directives.
2. Delete `include/mcp/util/tasks.hpp`.
3. Update include sites across the repository.

## Verification
* `python3 tools/checks/run_checks.py`
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release`
