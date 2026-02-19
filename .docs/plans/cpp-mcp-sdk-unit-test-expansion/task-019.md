# Task ID: [task-019]
# Task Name: [Expand Unit Tests: OAuth Client (PKCE/Step-up/Redirect policy)]

## Context
Add coverage for PKCE verification rules, authorization/token request building invariants, step-up retry limits, and token storage behavior.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (OAuth 2.1 + PKCE; step-up; token storage; SSRF mitigations)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md`
* `include/mcp/auth/oauth_client.hpp`
* `src/auth_oauth_client.cpp`
* `src/auth_oauth_client_disabled.cpp`
* `tests/auth_oauth_client_test.cpp`
* `tests/auth_oauth_client_disabled_test.cpp` (created in `task-001`)

## Output / Definition of Done
* `tests/auth_oauth_client_test.cpp` adds tests for:
  - PKCE metadata enforcement (`code_challenge_methods_supported` present; must include `S256`)
  - `resource` parameter present in BOTH authorization and token requests
  - redirect policy: reject scheme downgrade; reject unexpected host changes
  - step-up retries capped and loop-safe when repeated `insufficient_scope` challenges occur
  - token storage: scopes tracked per resource; overwrites behave as expected
* `tests/auth_oauth_client_disabled_test.cpp` validates (when `MCP_SDK_ENABLE_AUTH=OFF`):
  - OAuth client entrypoints throw `OAuthClientError` with code `kSecurityPolicyViolation`
  - error message is actionable (mentions build-time disable)

## Step-by-Step Instructions
1. Add PKCE negative tests where metadata omits `code_challenge_methods_supported` or lacks `S256`.
2. Add tests verifying `resource` encoding in both URL query and token POST body.
3. Add step-up loop tests using mocked HTTP responses and token storage.
4. Implement the auth-disabled tests with preprocessor guards so they validate disabled behavior when compiled with auth off.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_auth_oauth_client_test_authorization --output-on-failure`
