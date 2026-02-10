# Task ID: [task-033]
# Task Name: [Conformance: Tasks]

## Context
Validate tasks state machine semantics, error codes, metadata rules, and access-control binding behavior.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Tasks acceptance requirements)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/tasks.md`

## Output / Definition of Done
* `tests/conformance/test_tasks.cpp` covers:
  - task augmentation returns CreateTaskResult
  - state transitions and terminal immutability
  - `tasks/result` blocking semantics
  - `-32602` for invalid taskId/cursor
  - related-task `_meta` injection rules
  - cancellation rejection for terminal tasks

## Step-by-Step Instructions
1. Add a deterministic fake long-running operation that transitions through states.
2. Write tests for each required rule.
3. Add tests for auth-context binding (using mock auth contexts).

## Verification
* `ctest --test-dir build -R tasks`
