# Task ID: task-003
# Task Name: Normalize MCP namespaces in `src/**/*.cpp` to match the derived namespace

## Context

After translation unit relocation, this task is responsible for updating namespace blocks in `.cpp` files so that opened MCP namespaces match the namespace derived from the translation unit directory.

## Inputs

- Output of `task-002`
- SRS: `.docs/requirements/cpp-source-namespace-and-include-normalization.md` (Functional Requirements 2)

## Output / Definition of Done

- Each non-anonymous MCP namespace opened in `src/**/*.cpp` is required to match the namespace derived from the translation unit path.
- `namespace mcp::detail::detail` is required to not exist.
- `namespace mcp::<module>::detail::detail` is required to not exist.

## Verification

- The repository must build after namespace changes using the build commands defined in the SRS.
