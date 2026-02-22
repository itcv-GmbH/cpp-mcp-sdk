# Task ID: task-031
# Task Name: Resolve Remaining Enforcement Violations

## Context
This task is responsible for producing a zero-violation result from all enforcement scripts after the planned file relocations, include updates, and public header splits.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Enforcement)
*   `tools/checks/run_checks.py`

## Dependencies
*   This task depends on: `task-005`, `task-006`, `task-007`, `task-008`, `task-009`, `task-010`, `task-011`, `task-012`, `task-013`, `task-014`, `task-015`, `task-016`, `task-017`, `task-018`, `task-019`, `task-020`, `task-021`, `task-022`, `task-023`, `task-024`, `task-025`, `task-026`, `task-027`, `task-028`, `task-029`, `task-030`.

## Output / Definition of Done
*   `python3 tools/checks/run_checks.py` exits zero.
*   `tools/checks/check_public_header_one_type.py` reports zero violations for all files under `include/mcp/`.
*   `tools/checks/check_include_policy.py` reports zero violations for all files under `src/` and `tests/`.
*   `tools/checks/check_git_index_hygiene.py` reports zero violations for the git index required-absent path set.

## Step-by-Step Instructions
1.  Run `python3 tools/checks/run_checks.py`.
2.  For each `check_public_header_one_type.py` violation, update the violating header so it contains at most one top-level `class` or `struct` definition and ensure all other types are moved into per-type headers.
3.  For each `check_include_policy.py` violation, replace directory-traversing relative includes with `<mcp/...>` includes and remove all test includes that target `src/`.
4.  For each `check_git_index_hygiene.py` violation, remove the violating path from the git index and update `.gitignore` so the violating path remains ignored.
5.  Re-run `python3 tools/checks/run_checks.py` until the exit code is zero.

## Verification
*   `python3 tools/checks/run_checks.py`
