# Review Report: task-008 (/ Expand Unit Tests: JSON-RPC Router)

## Status
**FAIL**
*(Note: Use PASS only if the code is perfect, secure, matches the plan, and tests pass.)*

## Compliance Check
- [x] Implementation matches `task-008.md` instructions.
- [ ] Definition of Done met.
- [x] No unauthorized architectural changes.

## Verification Output
*   **Command Run:** `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_jsonrpc_router_test --output-on-failure`
*   **Result:** Pass. `mcp_sdk_jsonrpc_router_test` passed (1/1).
*   **Command Run:** `git diff --name-only main...HEAD`
*   **Result:** Pass. Only `tests/jsonrpc_router_test.cpp` changed; no production code modifications detected.
*   **Command Run:** `rg --line-number "sleep_for" tests/jsonrpc_router_test.cpp`
*   **Result:** Fail for determinism requirement. Sleep-based polling remains at `tests/jsonrpc_router_test.cpp:73`.

## Issues Found (If FAIL)
*   **Critical:** None.
*   **Major:** Tests are not fully deterministic per review criteria because `waitForMessageCount(...)` uses `std::this_thread::sleep_for(...)`, introducing timing-based polling.
*   **Minor:** None.

## Required Actions
1. Replace sleep-based polling in `tests/jsonrpc_router_test.cpp` with deterministic synchronization (for example, promise/condition-variable signaling from the outbound sender callback).
2. Update affected assertions to wait on explicit events instead of elapsed time so the suite satisfies the "no sleeps" requirement.
