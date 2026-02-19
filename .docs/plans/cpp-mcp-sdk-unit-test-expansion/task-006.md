# Task ID: [task-006]
# Task Name: [Expand Unit Tests: Schema Validator]

## Context
Increase coverage of schema validator surfaces beyond the current happy-path tests, focusing on metadata reporting and definition selection.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Schema Validation; conformance suite requirements)
* `.docs/requirements/mcp-spec-2025-11-25/schema/schema.json`
* `include/mcp/schema/validator.hpp`
* `tests/schema_validator_test.cpp`

## Output / Definition of Done
* `tests/schema_validator_test.cpp` adds tests for:
  - `Validator::metadata()` fields are populated (local path non-empty; sha256 present; upstream URL/ref present)
  - `validateInstance()` with explicit `definitionName` succeeds for known definitions and fails for unknown definitions with diagnostics
  - `validateMcpMethodMessage()` behavior for unknown methods (ensures graceful validation result with clear diagnostics)
  - `formatDiagnostics()` includes key fields for multiple diagnostics

## Step-by-Step Instructions
1. Add a test asserting `Validator::loadPinnedMcpSchema().metadata()` has non-empty required fields.
2. Add a test that calls `validateInstance` with a known definition (e.g., `JSONRPCRequest`) and validates `valid=true` for a minimal valid instance.
3. Add a test calling `validateInstance` with a bogus definition name and assert `valid=false` with a diagnostic message that mentions the definition.
4. Add a test exercising `formatDiagnostics` when multiple diagnostics are produced (e.g., missing required keys + wrong types).

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_schema_validator_test --output-on-failure`
