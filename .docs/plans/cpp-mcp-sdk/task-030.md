# Task ID: [task-030]
# Task Name: [Pinned Mirror Verification Tests]

## Context
Ensure the conformance test suite fails fast if pinned normative inputs are missing or changed.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Mirror completeness requirement)
* `.docs/requirements/mcp-spec-2025-11-25/MANIFEST.md`

## Output / Definition of Done
* `tests/conformance/test_pinned_mirror.cpp` verifies:
  - every file listed in `MANIFEST.md` exists
  - the SDK tests load schema from pinned path

## Step-by-Step Instructions
1. Implement a test that parses `MANIFEST.md` and checks file existence under `.docs/requirements/mcp-spec-2025-11-25/`.
2. Ensure schema validator loads the pinned `schema/schema.json` path.
3. Add CI assertion to run this test early.

## Verification
* `ctest --test-dir build -R pinned_mirror`
