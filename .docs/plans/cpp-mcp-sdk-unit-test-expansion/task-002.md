# Task ID: [task-002]
# Task Name: [Unit Tests: Cancellation Helpers]

## Context
Increase direct unit coverage of cancellation helper parsing/building logic used across router/tasks semantics.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Cancellation invariants; conformance testing requirements)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/cancellation.md`
* `include/mcp/util/cancellation.hpp`
* `tests/util_cancellation_test.cpp` (created in `task-001`)

## Output / Definition of Done
* `tests/util_cancellation_test.cpp` contains comprehensive tests for:
  - `jsonToRequestId` (string/int64/uint64-in-range/out-of-range)
  - `requestIdToJson` round-trips for string and int IDs
  - `parseCancelledNotification` accepts valid payloads and rejects invalid shapes/types
  - `makeCancelledNotification` populates `method`, `params.requestId`, optional `reason`
  - `isTaskAugmentedRequest` behavior for absent/non-object `params` and non-object `params.task`
  - `extractTaskId` behavior for missing/empty/non-string values
  - `extractCreateTaskResultTaskId` extracts `result.task.taskId` only from success responses
  - `makeTasksCancelRequest` constructs correct method and params
* Tests are deterministic and do not rely on timing.

## Step-by-Step Instructions
1. Replace placeholder tests in `tests/util_cancellation_test.cpp` with table-driven cases for `jsonToRequestId` and parsing helpers.
2. Add negative-path tests for missing keys and wrong JSON types.
3. Add at least one end-to-end test that:
   - builds a cancelled notification via `makeCancelledNotification`
   - feeds it through `parseCancelledNotification`
   - verifies extracted `requestId` and `reason`.
4. Ensure tests cover both integer and string request IDs.

## Verification
* `cmake --build build/vcpkg-unix-release --target mcp_sdk_test_util_cancellation`
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_util_cancellation_test --output-on-failure`
