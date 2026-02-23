# Task ID: task-004
# Task Name: Normalize repository header includes in `src/**/*.cpp` and remove umbrella includes

## Context

This task is responsible for normalizing project header include directives in `.cpp` files and for removing umbrella header includes from translation units.

## Inputs

- Output of `task-003`
- SRS: `.docs/requirements/cpp-source-namespace-and-include-normalization.md` (Functional Requirements 3 and 4)

## Output / Definition of Done

- Any `#include` in `src/**/*.cpp` that targets a header under `include/mcp/**` is required to use double quotes and to begin with `mcp/`.
- `src/**/*.cpp` is required to not include `mcp/all.hpp`.
- `src/**/*.cpp` is required to not include `mcp/<module>/all.hpp`.

## Verification

- The repository must build after include normalization using the build commands defined in the SRS.
