# Task ID: [task-016]
# Task Name: [Expand Unit Tests: HTTP Runtime (URL/TLS error paths)]

## Context
Add unit tests for `HttpClientRuntime`/`HttpServerRuntime` error paths and URL parsing validation to reduce regressions in real-network usage.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (HTTPS requirements; security best practices references)
* `include/mcp/transport/http.hpp`
* `src/transport/http_runtime.cpp`
* `tests/transport_http_runtime_test.cpp` (created in `task-001`)
* `tests/transport_http_tls_test.cpp`

## Output / Definition of Done
* `tests/transport_http_runtime_test.cpp` adds tests for:
  - invalid endpoint URLs (missing scheme, unsupported scheme, empty host, invalid IPv6 authority) throw `std::invalid_argument`
  - HTTPS endpoint with `MCP_SDK_ENABLE_TLS=OFF` throws an actionable error (guard test with preprocessor)
  - server TLS config missing cert/key is rejected
* `tests/transport_http_tls_test.cpp` adds/adjusts tests to:
  - guard HTTPS-only assertions behind `#if MCP_SDK_ENABLE_TLS`

## Step-by-Step Instructions
1. Implement invalid URL table tests by constructing `HttpClientRuntime` with the invalid endpoint and asserting construction or `execute` throws (whichever is applicable).
2. Add TLS-disabled conditional test blocks where appropriate.
3. Extend TLS runtime tests to cover missing certificate/key errors.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_transport_http_tls_test --output-on-failure`
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_transport_http_runtime_test --output-on-failure`
