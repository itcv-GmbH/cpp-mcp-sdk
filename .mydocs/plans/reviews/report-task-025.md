# Review Report: task-025 (/ Split Server Runner Headers)

## Status
**FAIL**
*(Note: Use PASS only if the code is perfect, secure, matches the plan, and tests pass.)*

## Compliance Check
- [ ] Implementation matches `task-025.md` instructions.
- [ ] Definition of Done met.
- [ ] No unauthorized architectural changes.

## Verification Output
*   **Command Run:** `python3 tools/checks/check_public_header_one_type.py`
*   **Result:** **Fail** - enforcement check reported 22 violating public headers.
*   **Command Run:** `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
*   **Result:** **Pass** - configure/build succeeded and 53/53 tests passed.

## Issues Found (If FAIL)
*   **Critical:** Required enforcement command failed, so Definition of Done is not met.
*   **Major:** The task introduced additional public headers and scope expansion (`include/mcp/server/detail/runner_rules.hpp`, `include/mcp/server/detail/server_factory.hpp`, plus `CMakeLists.txt` changes) beyond the split contract.
*   **Minor:** Runner per-type declarations were moved into `include/mcp/server/detail/` rather than a straightforward per-type layout directly under `include/mcp/server/`, increasing API surface ambiguity.

## Required Actions
1. Rework task-025 to match the planned split exactly (only required runner/options type factoring and umbrella updates).
2. Make `python3 tools/checks/check_public_header_one_type.py` pass for the branch baseline, then rerun full verification.
