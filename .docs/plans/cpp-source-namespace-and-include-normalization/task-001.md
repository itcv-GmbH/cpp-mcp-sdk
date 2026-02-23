# Task ID: task-001
# Task Name: Inventory `src/**/*.cpp` namespace-to-path violations

## Context

This task is responsible for producing a complete, deterministic inventory of `.cpp` files under `src/` whose MCP namespaces do not match the namespace derived from their directory path.

## Inputs

- `src/`
- SRS: `.docs/requirements/cpp-source-namespace-and-include-normalization.md` (Functional Requirements 2)

## Output / Definition of Done

- A list of violating translation units is required to exist in this task output.
- Each entry is required to include:
  - file path
  - first MCP namespace opened in the file
  - expected namespace derived from the file path
- The inventory is required to exclude `tests/` and `examples/`.

## Verification

- The inventory is required to be reproducible from a clean checkout.
