# Task ID: task-031
# Task Name: Resolve Remaining Enforcement Violations

## Context
This task is responsible for running the enforcement scripts after the planned header splits and include updates and resolving any remaining violations.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Enforcement)
*   `tools/checks/run_checks.py`

## Output / Definition of Done
*   `python3 tools/checks/run_checks.py` exits zero.
*   All public headers under `include/mcp/` satisfy the one-type-per-header rule.
*   All files under `src/` and `tests/` satisfy the include traversal rule.
*   The git index satisfies the required-absent path set.

## Step-by-Step Instructions
1.  Run `python3 tools/checks/run_checks.py`.
2.  For each violation reported, apply the corresponding refactor pattern already defined in the Phase 2 tasks and update the affected header or include usage.
3.  Re-run `python3 tools/checks/run_checks.py` and verify the exit code is zero.

## Verification
*   `python3 tools/checks/run_checks.py`
