# Task ID: task-001
# Task Name: Repository Hygiene Baseline

## Context
This task is responsible for enforcing repository hygiene requirements by ensuring generated artifacts and environment directories are absent from the git index and are ignored for future changes.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Repository Hygiene)
*   `.gitignore`

## Dependencies
*   This task depends on no other plan tasks.

## Output / Definition of Done
*   The git index contains zero entries matching:
    *   `build/`
    *   `tests/integration/.venv/`
    *   `**/__pycache__/`
    *   `**/*.pyc`
*   `.gitignore` includes ignore rules that match the required paths.

## Step-by-Step Instructions
1.  Run `git ls-files` and verify the git index contains zero matches for the required-absent paths.
2.  Remove any matched tracked paths from the git index using `git rm -r --cached`.
3.  Update `.gitignore` to ignore the required-absent paths exactly.
4.  Run `git status` and verify the working tree contains the `.gitignore` change and contains no staged generated artifacts.

## Verification
*   `git ls-files | rg -n "^(build/|tests/integration/\.venv/|.*__pycache__/.*|.*\.pyc)$" || true`
*   `git status --porcelain`
