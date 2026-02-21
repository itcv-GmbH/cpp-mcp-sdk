# Task ID: task-002
# Task Name: Implement Default SSE Retry Waiting

## Context

MCP requires that clients respect SSE `retry` guidance by waiting the specified number of milliseconds before attempting to reconnect. The current implementation only delays when `StreamableHttpClientOptions.waitBeforeReconnect` is provided, which violates the transport requirement when the hook is unset.

## Inputs

* `.docs/requirements/cpp-mcp-sdk.md` section: "Streamable HTTP (Required)" and "Streamable HTTP SSE Semantics"
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md` section: "Sending Messages to the Server" (retry field requirements)
* `include/mcp/transport/http.hpp` (`StreamableHttpClientOptions.waitBeforeReconnect`)
* `src/transport/http_client.cpp` (`waitForReconnect`, `pollListenStream`, POST-stream resumption)
* `tests/transport_http_client_test.cpp`

## Output / Definition of Done

* `src/transport/http_client.cpp` will delay reconnect attempts by default when `waitBeforeReconnect` is unset.
* The delay will be bounded by `security::RuntimeLimits.maxRetryDelayMilliseconds`.
* Tests will avoid wall-clock sleeping by overriding `waitBeforeReconnect`.
* `ctest --test-dir build/vcpkg-unix-release -R transport_http_client` will pass.

## Step-by-Step Instructions

1. Update `waitForReconnect` in `src/transport/http_client.cpp` to perform a real sleep when `options.waitBeforeReconnect` is unset.
2. Ensure the sleep duration uses the effective retry delay after clamping.
3. Update affected unit tests to set `StreamableHttpClientOptions.waitBeforeReconnect` explicitly in all code paths that invoke reconnection logic.
4. Add a unit test that asserts the effective retry delay value that would be waited is the clamped value. The test must validate this by injecting `waitBeforeReconnect` and recording the received delay.

## Verification

* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R transport_http_client -V`
