# Review Report: task-029 (/ Normalize `error_reporter`, `errors`, `version` Basenames)

## Status
**FAIL**
*(Note: Use PASS only if the code is perfect, secure, matches the plan, and tests pass.)*

## Compliance Check
- [ ] Implementation matches `task-029.md` instructions.
- [ ] Definition of Done met.
- [ ] No unauthorized architectural changes.

## Verification Output
*   **Command Run:** `python3 tools/checks/check_public_header_one_type.py`
*   **Result:** **Fail** - enforcement check reported 25 violating public headers.
*   **Command Run:** `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
*   **Result:** **Pass** - configure/build succeeded and 53/53 tests passed.

## Issues Found (If FAIL)
*   **Critical:** Required output headers from the task contract are missing (`include/mcp/error_event.hpp`, `include/mcp/json_rpc_error.hpp`, `include/mcp/negotiated_protocol_version.hpp`).
*   **Major:** Implementation diverged to a broad `include/mcp/sdk/*` migration and touched a large unrelated surface area (examples, transport, lifecycle, tests), which is unauthorized architectural drift for this task.
*   **Minor:** Deprecation wrappers in `include/mcp/error_reporter.hpp`, `include/mcp/errors.hpp`, and `include/mcp/version.hpp` do not satisfy the requested umbrella include targets.

## Required Actions
1. Implement exactly the three required basename-normalization outputs in `include/mcp/` and wire umbrellas to those new headers.
2. Remove/revert unrelated migration scope from this task branch, then rerun enforcement/build/test verification.
