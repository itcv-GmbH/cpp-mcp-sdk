# Task ID: task-015
# Task Name: Full Regression + Conformance Run

## Context
After DRY refactors, API cleanup, and concurrency hardening, run the full test suite to ensure no protocol regressions against the SRS and pinned MCP spec mirror.

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Acceptance Criteria; Conformance Testing)
*   `.docs/requirements/mcp-spec-2025-11-25/` (pinned normative spec mirror)
*   All code changes from `task-001` through `task-014`

## Output / Definition of Done
*   Clean build completes on the primary preset.
*   All unit tests and conformance tests pass.
*   Any new/changed tests are deterministic (no hangs; no long sleeps).

## Step-by-Step Instructions
1.  Configure and build using the recommended preset:
    *   `cmake --preset vcpkg-unix-release`
    *   `cmake --build build/vcpkg-unix-release`
2.  Run the full suite:
    *   `ctest --test-dir build/vcpkg-unix-release --output-on-failure`
3.  If failures occur:
    *   bisect whether the failure is from helper semantics drift (ascii/url/base64) vs API cleanup vs Streamable HTTP locking
    *   add/adjust unit tests to lock the intended behavior before changing production code further

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
