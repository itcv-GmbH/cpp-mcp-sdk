# Task ID: [task-028]
# Task Name: [OAuth 2.1 + PKCE + Loopback Redirect Receiver]

## Context
Implement OAuth 2.1 authorization code flow with PKCE (S256), plus an embedded loopback redirect receiver to capture authorization codes securely.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (OAuth 2.1 + PKCE; loopback receiver)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md`

## Output / Definition of Done
* `include/mcp/auth/oauth_client.hpp` provides:
   - PKCE code verifier/challenge generator (S256)
   - authorization URL builder (with `resource` per RFC8707)
   - token exchange request builder
* `include/mcp/auth/loopback_receiver.hpp` provides:
  - start local HTTP listener on localhost
  - capture `code` and `state`
  - timeouts and CSRF protections
* Does not include a browser opener; host app opens URL

## Step-by-Step Instructions
1. Verify PKCE support via discovered authorization server metadata:
   - require `code_challenge_methods_supported` to be present
   - require `S256` to be supported; otherwise refuse to proceed
2. Implement PKCE S256 utilities.
3. Implement loopback receiver:
   - bind to 127.0.0.1 only
   - random available port
   - verify state matches
   - return a simple HTML success page (optional)
   - ensure redirect URIs are `localhost` (http) or `https` as required by spec
4. Implement token request (authorization code) using discovered token endpoint.
   - include `resource` parameter in BOTH authorization request and token request
5. Ensure tokens are never placed in URLs.
6. Add tests for state mismatch and timeout behavior.

## Verification
* `ctest --test-dir build`
