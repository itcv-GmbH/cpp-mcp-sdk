# Task ID: task-002
# Task Name: Relocate `src/**/*.cpp` files to align with namespace layout

## Context

This task is responsible for moving `.cpp` files within `src/` so that each file's directory path derives the MCP namespace implemented by that file.

## Inputs

- Output of `task-001`
- SRS: `.docs/requirements/cpp-source-namespace-and-include-normalization.md` (Functional Requirements 2)

## Output / Definition of Done

- Any `.cpp` file that implements MCP symbols under a namespace that is not derived from its current directory path is required to be relocated under `src/`.
- No `.cpp` files are required to remain at the `src/` root unless they implement symbols under `namespace mcp` derived from `src/`.

## Verification

- The repository must build after relocations using the build commands defined in the SRS.
