# Task ID: [task-029]
# Task Name: [Step-up Auth Retries + Token Storage + SSRF/Redirect Hardening]

## Context
Complete the auth story: secure token storage interfaces, automatic retry on insufficient_scope challenges (with loop protection), and hardened network fetch policies.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Step-up authorization; secure token storage; SSRF mitigations)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/security_best_practices.md`

## Output / Definition of Done
* Token storage interface (host-pluggable) with a safe default (in-memory, session-scoped)
* Step-up flow:
   - on 403 + `WWW-Authenticate` `error="insufficient_scope"`, request additional scopes (from `scope` challenge) and retry with limits
* SSRF and redirect policy enforced for discovery and token endpoint interactions
* Tests cover retry loop limits and redirect scheme/origin restrictions

## Step-by-Step Instructions
1. Define token cache/storage interface and default implementation.
2. Implement 401/403 handling and step-up authorization behavior with retry caps.
   - parse `WWW-Authenticate` `scope` and `resource_metadata`
   - avoid loops by tracking attempted scope sets per resource
3. Implement strict redirect validation:
   - HTTPS-only
   - no redirects to different origins unless explicitly allowed
   - block private IP ranges for discovery fetches
4. Add tests using a local HTTP server simulating redirects and insufficient_scope.

## Verification
* `ctest --test-dir build`
