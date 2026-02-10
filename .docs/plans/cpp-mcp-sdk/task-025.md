# Task ID: [task-025]
# Task Name: [Cancellation + Progress Utilities]

## Context
Implement cancellation and progress utilities end-to-end, including invariants (initialize not cancellable; monotonic progress; stop on completion).

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Cancellation; Progress)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/cancellation.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/progress.md`

## Output / Definition of Done
* `include/mcp/util/cancellation.hpp` supports sending/handling `notifications/cancelled`
* `include/mcp/util/progress.hpp` supports progress token management and notifications
* Enforces:
   - cannot cancel initialize
   - cancellation references only in-flight request IDs for that direction
   - task-augmented request cancellation uses `tasks/cancel` (not `notifications/cancelled`)
   - progress monotonicity and stop on completion

## Step-by-Step Instructions
1. Implement cancellation message helpers and router integration.
2. Implement progress token allocation and validation.
3. Integrate with long-running handlers and tasks.
4. Add conformance tests for invariants.

## Verification
* `ctest --test-dir build`
