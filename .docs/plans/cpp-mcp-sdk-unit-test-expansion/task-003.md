# Task ID: [task-003]
# Task Name: [Unit Tests: Progress Helpers]

## Context
Add direct unit tests for progress token extraction and progress notification parsing/building to harden progress semantics.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Progress invariants; conformance testing requirements)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/progress.md`
* `include/mcp/util/progress.hpp`
* `tests/util_progress_test.cpp` (created in `task-001`)

## Output / Definition of Done
* `tests/util_progress_test.cpp` contains comprehensive tests for:
  - `extractProgressToken` handling of missing `params`, missing `_meta`, wrong types
  - `jsonToNumber` behavior for int/uint/double and invalid types
  - `parseProgressNotification` acceptance/rejection matrix and correct `additionalProperties` capture
  - `makeProgressNotification` optional field emission (`total`, `message`)
  - `requestIdToJson`/`jsonToRequestId` round-trips for supported ID types
* Tests are deterministic.

## Step-by-Step Instructions
1. Implement table-driven parsing tests for `parseProgressNotification`:
   - required keys present/missing
   - numeric type variations for `progress` and `total`
   - string vs numeric tokens
2. Add a test that confirms `additionalProperties` excludes the reserved keys (`progressToken`, `progress`, `total`, `message`).
3. Add a test ensuring `makeProgressNotification` does not emit absent optional keys.

## Verification
* `cmake --build build/vcpkg-unix-release --target mcp_sdk_test_util_progress`
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_util_progress_test --output-on-failure`
