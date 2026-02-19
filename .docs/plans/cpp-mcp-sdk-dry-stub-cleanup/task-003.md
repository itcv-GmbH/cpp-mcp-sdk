# Task ID: task-003
# Task Name: Add Internal Absolute URL Parser + Tests

## Context
Authorization discovery, OAuth flows, and HTTP transports all parse absolute URLs with subtly different local implementations. A shared absolute URL parser reduces duplication and provides a single place to harden and test security invariants (userinfo rejection, whitespace/control rejection, IPv6 literal handling).

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Authorization discovery; HTTP security; SSRF/redirect hardening requirements)
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md`
*   Existing parsing logic in `src/auth_oauth_client.cpp`, `src/auth_protected_resource_metadata.cpp`, `src/auth_client_registration.cpp`, `src/transport/http_client.cpp`, `src/transport/http_runtime.cpp`

## Output / Definition of Done
*   `include/mcp/detail/url.hpp` added with a shared parser:
    *   `struct mcp::detail::ParsedAbsoluteUrl { scheme, host, port, path, optional query; flags for ipv6Literal, hasExplicitPort, hasQuery }`
    *   `mcp::detail::parseAbsoluteUrl(std::string_view rawUrl, /*options*/ ) -> std::optional<ParsedAbsoluteUrl>` (or throws with typed errors in callers)
*   `tests/detail_url_test.cpp` added to cover:
    *   http/https parsing (scheme/host/port/path/query)
    *   IPv6 literals in `[]` with optional port
    *   userinfo (`@`) rejection
    *   whitespace/control rejection
    *   default path normalization rules used by transport parsing
*   `tests/CMakeLists.txt` updated to build and register `mcp_sdk_detail_url_test`.

## Step-by-Step Instructions
1.  Implement `include/mcp/detail/url.hpp` as a low-level parser that:
    *   requires `scheme://` (absolute)
    *   rejects whitespace/control characters anywhere in the raw URL
    *   rejects any `@` in the authority component
    *   lowercases scheme/host using ASCII rules
    *   captures the request-target portion (`/path?query`) separately when needed
2.  Keep policy out of the parser: enforcement such as “HTTPS-only” should remain in higher-level call sites.
3.  Add `tests/detail_url_test.cpp` to lock the contract and document tricky cases.
4.  Register the new test target in `tests/CMakeLists.txt`.

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_detail_url_test --output-on-failure`
