# Task ID: [task-034]
# Task Name: [Conformance: Authorization]

## Context
Validate server-side 401/403 semantics, RFC9728 metadata publication, and client-side discovery + PKCE flow building.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Authorization requirements)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md`

## Output / Definition of Done
* `tests/conformance/test_authorization.cpp` covers:
  - 401 includes `resource_metadata` in `WWW-Authenticate`
  - well-known RFC9728 fallback behavior
  - RFC8414/OIDC discovery endpoint probing order
  - PKCE metadata verification (`code_challenge_methods_supported` must be present; require `S256`)
  - PKCE S256 generation correctness
  - `resource` parameter included in BOTH authorization and token requests
  - client registration strategy selection (pre-reg vs CIMD vs dynamic registration) when applicable
  - step-up retry limits

## Step-by-Step Instructions
1. Implement mock HTTP resource server + auth server endpoints for tests.
2. Validate challenge parsing and probing order.
3. Validate that tokens are not placed in URLs.
4. Add negative tests for SSRF/redirect policy.

## Verification
* `ctest --test-dir build -R authorization`
