# Task ID: task-006
# Task Name: Refactor OAuth PKCE Base64url

## Context
OAuth PKCE relies on correct base64url (unpadded) encoding. The SDK currently implements PKCE base64url separately from cursor encoding. This task moves PKCE encoding to the shared base64url helper to avoid drift.

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Authorization: OAuth 2.1 + PKCE S256)
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md`
*   Existing implementation in `src/auth_oauth_client.cpp`
*   Shared helper from `include/mcp/detail/base64url.hpp` (from `task-002`)

## Output / Definition of Done
*   `src/auth_oauth_client.cpp` updated to remove the local `base64UrlEncode()` and call `mcp::detail::encodeBase64UrlNoPad()`.
*   PKCE verifier/challenge length checks remain unchanged.
*   Authorization tests remain green.

## Step-by-Step Instructions
1.  Replace the local PKCE `base64UrlEncode(const std::vector<std::uint8_t>&)` implementation with a call to the shared helper.
2.  Ensure the shared helper produces the same output as the previous implementation:
    *   url-safe alphabet (`-`/`_`)
    *   no `=` padding
3.  Keep existing PKCE constraints (verifier length 43-128, S256 requirement).
4.  Update `src/auth_oauth_client.cpp` includes to reference `include/mcp/detail/base64url.hpp`.

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_auth_oauth_client_test_authorization --output-on-failure`
*   `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_conformance_authorization_test_authorization --output-on-failure`
