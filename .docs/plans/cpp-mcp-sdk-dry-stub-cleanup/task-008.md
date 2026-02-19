# Task ID: task-008
# Task Name: Refactor Auth ASCII + Absolute URL Parsing

## Context
OAuth discovery and client registration are security-critical and currently duplicate trimming, case-folding, token parsing, port parsing, and absolute URL parsing logic across multiple translation units. This task migrates auth code to use the shared ASCII and URL helpers while preserving the existing validation rules.

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Authorization; SSRF mitigations; redirect hardening)
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md`
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/security_best_practices.md`
*   Auth modules:
    *   `src/auth_oauth_client.cpp`
    *   `src/auth_protected_resource_metadata.cpp`
    *   `src/auth_client_registration.cpp`
    *   `src/auth_loopback_receiver.cpp`
*   Shared helpers from `include/mcp/detail/ascii.hpp` and `include/mcp/detail/url.hpp`

## Output / Definition of Done
*   Auth modules updated to use shared ASCII helpers (trim/lowercase/case-insensitive compare/whitespace+control detection).
*   Absolute URL parsing in auth modules migrated to `mcp::detail::parseAbsoluteUrl` (or equivalent shared parser), retaining module-level policy checks (https-only, localhost-loopback exceptions, redirect rules).
*   All auth unit tests and authorization conformance tests pass.

## Step-by-Step Instructions
1.  For each auth translation unit listed in Inputs:
    *   remove local copies of ASCII helpers and replace with `mcp::detail` equivalents.
2.  Migrate absolute URL parsing call sites:
    *   use shared parser for structural parsing (scheme/authority/path/query)
    *   keep existing policy decisions in-place (e.g., “authorization_endpoint must be HTTPS”).
3.  Ensure error messages remain actionable (keep existing error codes/enums; adapt messages only when necessary).
4.  Add or update tests when needed to prove behavior did not regress for:
    *   whitespace/control in URLs
    *   redirects changing scheme/origin
    *   localhost loopback redirect_uri rules

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_auth_protected_resource_metadata_test_authorization --output-on-failure`
*   `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_auth_client_registration_test_authorization --output-on-failure`
*   `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_auth_oauth_client_test_authorization --output-on-failure`
*   `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_conformance_authorization_test_authorization --output-on-failure`
