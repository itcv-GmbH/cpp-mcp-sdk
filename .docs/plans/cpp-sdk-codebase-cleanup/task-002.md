# Task ID: task-002
# Task Name: Add Deterministic Enforcement Scripts

## Context
This task is responsible for adding deterministic enforcement scripts that validate public header organization and include policy constraints.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Include Policy; Public Header Organization Rules; Enforcement)
*   `include/mcp/`
*   `src/`
*   `tests/`

## Dependencies
*   This task depends on no other plan tasks.

## Output / Definition of Done
*   `tools/checks/check_public_header_one_type.py` exists and exits non-zero when any file under `include/mcp/` defines more than one top-level `class` or `struct`.
*   `tools/checks/check_include_policy.py` exists and exits non-zero when any file under `src/` or `tests/` contains a directory-traversing relative include.
*   `tools/checks/check_git_index_hygiene.py` exists and exits non-zero when any required-absent path is present in the git index.
*   `tools/checks/run_checks.py` exists and runs all enforcement scripts with deterministic output ordering.
*   The scripts implement deterministic file ordering and deterministic diagnostic formatting.

## Step-by-Step Instructions
1.  Create `tools/checks/check_public_header_one_type.py`.
2.  Implement a comment- and string-literal-stripping lexer that scans each `include/mcp/**/*.hpp` file.
3.  Implement a brace-depth state machine that counts top-level `class` and `struct` definitions and excludes nested type definitions.
4.  Implement explicit exclusion for `enum class` and `enum struct` so the check counts only `class` and `struct` declarations.
5.  Create `tools/checks/check_include_policy.py`.
6.  Implement scanning of `src/**/*.{cpp,hpp}` and `tests/**/*.{cpp,hpp}` and fail on any `#include` whose include string contains `../` or `..\\`.
7.  Implement a test-only rule that fails on any `#include` that targets `src/` paths.
8.  Create `tools/checks/check_git_index_hygiene.py` that runs `git ls-files` and fails on any required-absent path.
9.  Create `tools/checks/run_checks.py` that executes the three checks and returns non-zero when any check fails.
10. Run `python3 tools/checks/run_checks.py` and record the current violation set as baseline output for the ongoing refactor branch.

## Verification
*   `python3 tools/checks/run_checks.py`
