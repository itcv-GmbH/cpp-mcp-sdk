# Review Report: task-011 - Server Module Headers

## Status
**FAIL**
*(Note: Use PASS only if the code is perfect, secure, matches the plan, and tests pass.)*

## Compliance Check
- [x] `include/mcp/server/server.hpp` declares `mcp::server::Server` and is not an umbrella header
- [x] `include/mcp/server/server_class.hpp` does not exist
- [x] Types in `include/mcp/server/*.hpp` declared in `namespace mcp::server`
- [ ] Types in `include/mcp/server/detail/*.hpp` declared in `namespace mcp::server::detail` **(FAILED)**
- [x] `include/mcp/server/all.hpp` exists and contains only `#include` directives
- [x] Prohibited umbrella headers don't exist outside `all.hpp`
- [x] Build succeeds
- [x] All 53 tests pass

## Verification Output
*   **Command Run:** `python3 tools/checks/run_checks.py`
*   **Result:** Pass. All 3 checks passed (check_public_header_one_type.py, check_include_policy.py, check_git_index_hygiene.py)

*   **Command Run:** `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
*   **Result:** Pass. Build succeeded and all 53 tests passed (100%)

## Issues Found

### Critical
*   **Namespace violation in detail headers:** All files under `include/mcp/server/detail/*.hpp` declare types in `namespace mcp::server` instead of the required `namespace mcp::server::detail`.

    **Affected files (all using `namespace mcp::server` instead of `namespace mcp::server::detail`):**
    - `include/mcp/server/detail/runner_rules.hpp` (line 5)
    - `include/mcp/server/detail/server_factory.hpp` (line 8)
    - `include/mcp/server/detail/streamable_http_server_runner.hpp` (line 9)
    - `include/mcp/server/detail/streamable_http_server_runner_options.hpp` (line 5)
    - `include/mcp/server/detail/stdio_server_runner.hpp` (line 15)
    - `include/mcp/server/detail/stdio_server_runner_options.hpp` (line 6)
    - `include/mcp/server/detail/combined_server_runner.hpp` (line 12)
    - `include/mcp/server/detail/combined_server_runner_options.hpp` (line 10)

    **Task requirement (from task-011.md):** "Every type declared under `include/mcp/server/detail/*.hpp` is required to be declared in `namespace mcp::server::detail`"

    **Current state:** All detail headers incorrectly use `namespace mcp::server`
    **Required state:** All detail headers must use `namespace mcp::server::detail`

## Required Actions
1.  **Fix namespace in all detail headers:** Change `namespace mcp::server` to `namespace mcp::server::detail` in all 8 files under `include/mcp/server/detail/*.hpp`.
2.  **Update all references:** Ensure all source files (`src/server/*.cpp`, tests, examples) that reference types from detail headers are updated to use the new namespace `mcp::server::detail` where appropriate.
3.  **Re-run verification:** After fixing, run both `python3 tools/checks/run_checks.py` and the full build/test sequence to ensure no regressions.
