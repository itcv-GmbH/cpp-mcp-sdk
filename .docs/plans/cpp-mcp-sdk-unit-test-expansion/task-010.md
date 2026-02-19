# Task ID: [task-010]
# Task Name: [Expand Unit Tests: Tasks Store + Receiver]

## Context
Broaden unit tests for task state transitions, TTL/limits behavior, related-task metadata injection, and auth-context binding.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Tasks utility requirements; access-control binding)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/tasks.md`
* `include/mcp/util/tasks.hpp`
* `src/util/tasks.cpp`
* `tests/tasks_test.cpp`

## Output / Definition of Done
* `tests/tasks_test.cpp` adds tests for:
  - invalid state transitions rejected (e.g., terminal -> non-terminal)
  - `ttl: null` support (no-expiry semantics)
  - cancellation semantics for running vs terminal tasks
  - related-task `_meta.io.modelcontextprotocol/related-task` is present where required
  - auth-context mismatch is rejected for task operations (no cross-context leakage)

## Step-by-Step Instructions
1. Add tests creating tasks with and without auth contexts and verifying access enforcement on `get/list/result/cancel`.
2. Add tests for `ttl` behavior including `ttl: null` and `ttl` above configured max.
3. Add tests verifying related-task metadata injection in responses.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_tasks_test --output-on-failure`
