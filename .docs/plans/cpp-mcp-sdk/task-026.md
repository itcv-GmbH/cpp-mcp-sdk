# Task ID: [task-026]
# Task Name: [Server-side Authorization (RFC9728 publication + 401/403 challenges)]

## Context
Implement server-side MCP Authorization requirements for HTTP transports: protected resource metadata publication and correct challenge semantics.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Authorization: Server-side)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md`

## Output / Definition of Done
* `include/mcp/auth/oauth_server.hpp` defines token verification interface and scope model
* Server returns:
  - 401 for missing/invalid token with `WWW-Authenticate` including `resource_metadata`
  - 403 for insufficient scope with `WWW-Authenticate` scope guidance
* Server publishes RFC9728 protected resource metadata at required well-known URI(s):
  - `/.well-known/oauth-protected-resource/<mcp-endpoint-path>` and/or
  - `/.well-known/oauth-protected-resource`
* Server validates audience binding and does not accept token passthrough

## Step-by-Step Instructions
1. Define auth context representation attached to each HTTP request.
2. Implement `WWW-Authenticate` challenge construction per spec.
   - 401: include `Bearer resource_metadata="..."` and (optionally) `scope="..."`
   - 403 insufficient scope: include `Bearer error="insufficient_scope"`, `scope="..."`, and `resource_metadata="..."`
3. Implement protected resource metadata endpoint(s) and configuration.
   - support both root and path-based well-known discovery forms
4. Integrate auth context into tasks binding (needed for task isolation).
5. Add tests for 401/403 behaviors and header parsing expectations.

## Verification
* `ctest --test-dir build`
