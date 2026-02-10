# Task ID: [task-024]
# Task Name: [Tasks Utility (task augmentation + tasks/* + status notifications)]

## Context
Implement MCP 2025-11-25 tasks utility, including task-augmented requests, task state machine, polling/result retrieval, and access control binding when auth context exists.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Tasks section; Related-task metadata rules)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/tasks.md`
* `.docs/requirements/mcp-spec-2025-11-25/schema/schema.ts` (Task, CreateTaskResult)

## Output / Definition of Done
* `include/mcp/util/tasks.hpp` defines task types and APIs
* Implements:
  - task augmentation via `params.task`
  - `tasks/get`, `tasks/result`, `tasks/list`, `tasks/cancel`
  - optional `notifications/tasks/status` send/receive
* Enforces:
  - receiver-generated task IDs (string; high entropy)
  - valid status transitions and terminal immutability
  - TTL behavior (`ttl: null` supported)
  - `tasks/result` blocks until terminal and returns underlying result or JSON-RPC error
  - `tasks/result` includes `_meta.io.modelcontextprotocol/related-task` in responses
  - invalid/nonexistent `taskId` or cursor -> `-32602`
  - auth-context binding when available

## Step-by-Step Instructions
1. Define task store interfaces:
   - in-memory reference implementation
   - abstraction for embedding external job systems
2. Implement task augmentation parsing and receiver behavior:
   - return CreateTaskResult quickly
3. Implement `tasks/get/list/cancel/result` handlers.
4. Implement related-task metadata injection rules.
5. Integrate with tools/call (and sampling/elicitation when task capabilities declared).
6. Add tests for state transitions, cancellation semantics, and `tasks/result` blocking behavior.

## Verification
* `ctest --test-dir build`
