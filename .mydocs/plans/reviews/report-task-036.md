# Review Report: task-036 (/ Examples (server/client; bidirectional sampling/elicitation/tasks))

## Status
**PASS**
*(Note: Use PASS only if the code is perfect, secure, matches the plan, and tests pass.)*

## Compliance Check
- [x] Implementation matches `task-[id].md` instructions.
- [x] Definition of Done met.
- [x] No unauthorized architectural changes.

## Verification Output
*   **Command Run:** `cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON`
*   **Result:** **Pass** - configured successfully with the vcpkg preset and generated build files under `build/vcpkg-unix-release`.

*   **Command Run:** `cmake --build build/vcpkg-unix-release`
*   **Result:** **Pass** - full build succeeded (library, tests, and examples).

*   **Command Run:** `cmake --build build/vcpkg-unix-release --target mcp_sdk_example_stdio_server mcp_sdk_example_http_server_auth mcp_sdk_example_http_client_auth mcp_sdk_example_bidirectional_sampling_elicitation`
*   **Result:** **Pass** - all task-036 example targets are buildable.

*   **Command Run:** `./build/vcpkg-unix-release/examples/http_client_auth/mcp_sdk_example_http_client_auth`
*   **Result:** **Pass** - end-to-end OAuth demo flow succeeds (discovery -> authorization redirect -> loopback callback -> token exchange).

*   **Command Run:** `python3 - <<'PY' ...` (spawned `examples/http_server_auth`, then POSTed `initialize`, `notifications/initialized`, and `tools/list` over HTTPS)
*   **Result:** **Pass** - correct client-driven lifecycle sequence works (`initialize` 200, `notifications/initialized` 202, `tools/list` 200).

*   **Command Run:** `python3 - <<'PY' ...` (POSTed `tools/list` before `initialize`)
*   **Result:** **Pass** - server returns JSON-RPC error `Method 'tools/list' is not valid before initialization completes.`, confirming self-initialization is removed.

*   **Command Run:** `python3 - <<'PY' ...` (unauthorized `POST /mcp` and metadata fetch)
*   **Result:** **Pass** - unauthorized request returns `401` with `WWW-Authenticate: Bearer resource_metadata=...`; protected-resource metadata endpoint returns expected JSON.

*   **Command Run:** `printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"manual-client","version":"0.1.0"}}}' '{"jsonrpc":"2.0","method":"notifications/initialized"}' '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' | ./build/vcpkg-unix-release/examples/stdio_server/mcp_sdk_example_stdio_server`
*   **Result:** **Pass** - stdio example initializes and returns tool metadata; demonstrates tools/resources/prompts with task-capable tooling.

*   **Command Run:** `./build/vcpkg-unix-release/examples/bidirectional_sampling_elicitation/mcp_sdk_example_bidirectional_sampling_elicitation`
*   **Result:** **Pass** - demonstrates bidirectional sampling/elicitation and task polling via `tasks/result`.

*   **Command Run:** `ctest --test-dir build/vcpkg-unix-release --output-on-failure`
*   **Result:** **Pass** - 28/28 tests passed.

## Issues Found (If FAIL)
*   **Critical:** None.
*   **Major:** None.
*   **Minor:** None.

## Required Actions
1.  None.
2.  None.
