# Review Report: task-021 (Client: Roots (handle roots/list + roots/list_changed))

## Status
**PASS**
*(Note: Use PASS only if the code is perfect, secure, matches the plan, and tests pass.)*

## Compliance Check
- [x] Implementation matches `task-021.md` instructions.
- [x] Definition of Done met.
- [x] No unauthorized architectural changes.

## Verification Output
*   **Command Run:** `cmake --build build && ctest --test-dir build`
*   **Result:** Pass. Build succeeded and all 14/14 tests passed.
*   **Command Run:** `./build/tests/mcp_sdk_test_client "[client][roots]"`
*   **Result:** Pass. Roots-focused tests passed (39 assertions in 4 test cases).
*   **Command Run:** `for i in 1 2 3 4 5; do ./build/tests/mcp_sdk_test_client "[client][roots]" >/dev/null || exit 1; done`
*   **Result:** Pass. Five repeated runs succeeded (deterministic execution observed).

## Issues Found (If FAIL)
*   **Critical:** None.
*   **Major:** None.
*   **Minor:** None.

## Required Actions
1. None.

## Notes
- `include/mcp/client/roots.hpp` exists and defines a stable roots provider interface (`RootEntry`, `RootsListContext`, `RootsProvider`).
- Inbound `roots/list` handling is capability/provider gated and returns JSON-RPC `-32601` (`"Roots not supported"`) with a reason when unsupported.
- Outbound `notifications/roots/list_changed` is emitted only when negotiated `roots.listChanged` is true and notification sending is allowed by session state.
- Root URI validation enforces `file://` and returns JSON-RPC `-32603` for invalid provider output.
- `.docs/plans/cpp-mcp-sdk/dependencies.md` only marks `task-021` complete; no unrelated task entries were altered.
