# Task ID: task-001
# Task Name: Implement Namespace Layout Enforcement Check (Not Yet Enabled)

## Context
This task will implement a deterministic automated check that fails when any `include/mcp/**` header declares types in namespaces that do not match directory path rules.

This task is responsible for creating the enforcement implementation without enabling it in CI yet, because the repository will violate the check until the refactor tasks are complete.

## Inputs
* `tools/checks/run_checks.py`
* Existing check patterns in `tools/checks/check_public_header_one_type.py`
* SRS: Functional Requirements 1 and 5
* SRS: Non-Functional Requirements Enforcement

## Output / Definition of Done
* `tools/checks/check_public_header_namespace_layout.py` is required to exist
* `tools/checks/check_public_header_namespace_layout.py` is required to validate namespace-to-path mapping for all `include/mcp/**/*.hpp`
* `tools/checks/check_public_header_namespace_layout.py` is required to fail on `namespace mcp::detail::detail`
* `tools/checks/check_public_header_namespace_layout.py` is required to be deterministic in output ordering
* `tools/checks/run_checks.py` is required to remain unchanged in this task

## Step-by-Step Instructions
1. Create `tools/checks/check_public_header_namespace_layout.py`.
2. The script is required to enumerate headers under `include/mcp/**/*.hpp` in a deterministic sorted order.
3. The script is required to derive an expected namespace from the header path:
   - `include/mcp/<module>/...` is required to map to `namespace mcp::<module>::...`
   - `include/mcp/<module>/detail/...` is required to map to `namespace mcp::<module>::detail::...`
   - `include/mcp/detail/...` is required to map to `namespace mcp::detail::...`
4. The script is required to detect top-level `class`, `struct`, and `enum` declarations and validate that each declaration is enclosed by the expected namespace.
5. The script is required to ignore declarations that are nested inside a `class` or `struct` body.
6. The script is required to produce actionable error output that lists each violating file and the first violating declaration line.
7. The script is required to exit with status code 1 when any violation is detected.

## Verification
* `python3 tools/checks/check_public_header_namespace_layout.py`
