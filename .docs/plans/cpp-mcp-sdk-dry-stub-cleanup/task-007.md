# Task ID: task-007
# Task Name: Refactor HTTP Header/Origin ASCII Helpers

## Context
HTTP header matching and Origin validation must be ASCII case-insensitive and whitespace-aware. Today, `include/mcp/transport/http.hpp` and `include/mcp/security/origin_policy.hpp` each define their own ASCII helpers. This task migrates both to the shared internal ASCII helper to ensure consistent behavior.

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (HTTP security requirements; Origin validation)
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/security_best_practices.md`
*   Existing helpers in `include/mcp/transport/http.hpp` and `include/mcp/security/origin_policy.hpp`
*   Shared helper from `include/mcp/detail/ascii.hpp` (from `task-001`)

## Output / Definition of Done
*   `include/mcp/transport/http.hpp` updated to use `mcp::detail` ASCII helpers for header matching and trimming.
*   `include/mcp/security/origin_policy.hpp` updated to use `mcp::detail` ASCII helpers for normalization.
*   Streamable HTTP and HTTP-common tests continue to pass.

## Step-by-Step Instructions
1.  Update `include/mcp/transport/http.hpp`:
    *   replace inline `detail::toLowerAscii`, `detail::equalsIgnoreCase`, `detail::trimAsciiWhitespace` with calls into `mcp::detail` helpers.
    *   keep any transport-specific helpers (protocol version validation) local.
2.  Update `include/mcp/security/origin_policy.hpp` similarly:
    *   use centralized trim/lowercase helpers
    *   ensure existing behavior around bracketed IPv6 host normalization is preserved
3.  Avoid introducing ODR/link issues: keep helpers header-only and `inline` where appropriate.

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_transport_http_common_test --output-on-failure`
*   `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_conformance_streamable_http_transport_test --output-on-failure`
