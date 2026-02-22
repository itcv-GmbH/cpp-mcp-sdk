# Task ID: task-015
# Task Name: Enable Namespace Layout Enforcement In CI Checks

## Context
This task will enable the namespace layout enforcement check so that CI fails on future namespace-to-path regressions.

## Inputs
* `tools/checks/run_checks.py`
* `tools/checks/check_public_header_namespace_layout.py`
* SRS: Non-Functional Requirements Enforcement

## Output / Definition of Done
* `tools/checks/run_checks.py` is required to execute `check_public_header_namespace_layout.py`
* CI is required to execute the enabled check via the existing `python3 tools/checks/run_checks.py` step
* The repository is required to pass all quality gates defined in the SRS

## Step-by-Step Instructions
1. Add `check_public_header_namespace_layout.py` to `CHECK_SCRIPTS` in `tools/checks/run_checks.py`.
2. Ensure the check passes on a clean checkout.
3. Ensure CI executes the check as part of `python3 tools/checks/run_checks.py`.

## Verification
* `python3 tools/checks/run_checks.py`
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
* `cmake --build build/vcpkg-unix-release --target clang-format-check`
