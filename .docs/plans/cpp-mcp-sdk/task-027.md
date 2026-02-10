# Task ID: [task-027]
# Task Name: [Client-side Discovery (WWW-Authenticate + RFC9728 + RFC8414/OIDC)]

## Context
Implement client-side discovery flow to find authorization server endpoints and required scopes.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Authorization: Client-side discovery)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md`

## Output / Definition of Done
* `include/mcp/auth/protected_resource_metadata.hpp` defines parsing/validation types
* Client can:
   - parse `WWW-Authenticate` `resource_metadata` and `scope`
   - fall back to RFC9728 well-known URI probing
   - discover authorization server metadata via RFC8414 and OIDC endpoints (including path-insertion variants)
   - apply MCP scope selection strategy (use `scope` challenge when present; otherwise use `scopes_supported`)
   - refuse insecure discovery (HTTPS-only; strict redirect policy)

## Step-by-Step Instructions
1. Implement robust `WWW-Authenticate` parser for Bearer challenges (multiple headers; parameters).
2. Implement RFC9728 probing order (when `resource_metadata` is absent):
    - `/.well-known/oauth-protected-resource/<mcp-endpoint-path>`
    - `/.well-known/oauth-protected-resource`
3. Parse Protected Resource Metadata and select an authorization server from `authorization_servers` (per RFC9728 guidance).
4. Implement authorization server metadata discovery sequence (RFC8414 + OIDC variants), including path-insertion rules.
5. Enforce transport security for discovery and metadata fetches:
    - HTTPS-only
    - strict redirect validation (no scheme downgrade; no unexpected origin changes)
    - SSRF mitigations (block private IP ranges; validate DNS results; limit redirects)
6. Implement scope selection strategy:
    - prefer `scope` from 401/403 challenges when present
    - otherwise use `scopes_supported` from protected resource metadata (or omit scope if undefined)
7. Add unit tests with canned HTTP responses.

## Verification
* `ctest --test-dir build`
