# Review Report: task-015 (/ Expand Unit Tests: Streamable HTTP Server)

## Status
**FAIL**
*(Note: Use PASS only if the code is perfect, secure, matches the plan, and tests pass.)*

## Compliance Check
- [ ] Implementation matches `task-[id].md` instructions.
- [ ] Definition of Done met.
- [x] No unauthorized architectural changes.

## Verification Output
*   **Command Run:** `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_transport_http_server_test --output-on-failure`
*   **Result:** Pass. `mcp_sdk_transport_http_server_test` completed successfully (`1/1` tests passed).

## Issues Found (If FAIL)
*   **Critical:** The new Content-Type rejection test uses body `{}` (`tests/transport_http_server_test.cpp`), which is already an invalid JSON-RPC payload. This can return HTTP 400 even when Content-Type validation is absent, so the test does not prove the required `missing/invalid Content-Type -> 400` behavior.
*   **Major:** Step 3 in `task-015.md` asks for resume tests covering bad `Last-Event-ID` formats, but no test asserts malformed `Last-Event-ID` is rejected with HTTP 400 (the server has an explicit `Invalid Last-Event-ID` branch that remains unverified).
*   **Minor:** None.

## Required Actions
1. Update the Content-Type test to use a valid single JSON-RPC POST body (request/notification/response) and assert HTTP 400 specifically for missing and invalid `Content-Type` values.
2. Add a GET resume test with malformed `Last-Event-ID` (e.g., non-parseable value) and assert the expected HTTP 400 rejection/status path.
