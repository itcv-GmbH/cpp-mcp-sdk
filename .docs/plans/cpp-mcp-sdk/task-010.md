# Task ID: [task-010]
# Task Name: [Implement Streamable HTTP Common (Headers, Session, Origin Policy)]

## Context
Implement the protocol-level HTTP rules shared by both server and client: headers, session ID, protocol version header, and origin validation.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP; Session Management; Protocol Version Header; HTTP Security)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`

## Output / Definition of Done
* `include/mcp/transport/http.hpp` defines:
   - MCP endpoint configuration
   - session state (`MCP-Session-Id` capture + replay)
   - `MCP-Protocol-Version` header handling
* `include/mcp/security/origin_policy.hpp` defines origin validation configuration
* Tests cover:
   - invalid/unsupported `MCP-Protocol-Version` -> HTTP 400 (server)
   - invalid `Origin` -> HTTP 403 (server)
   - session required but missing `MCP-Session-Id` -> HTTP 400 (server)
   - terminated/expired session -> HTTP 404 (server)

## Step-by-Step Instructions
1. Define HTTP header constants and helper functions.
2. Implement session ID capture from initialize HTTP response and propagation on subsequent requests.
   - server may require session IDs post-initialize; if required and missing, respond HTTP 400
   - if a session is terminated/expired, respond HTTP 404 for that `MCP-Session-Id`
   - client receiving HTTP 404 for a session MUST reinitialize (new session)
   - client SHOULD support HTTP DELETE with `MCP-Session-Id` to explicitly terminate sessions; server MAY respond 405
3. Implement protocol version header rules (send after init; validate on server).
   - server MAY rely on negotiated version instead of header when it can (session-bound)
   - if header is missing and server has no other way to identify version, server SHOULD assume `2025-03-26` (per transport spec)
4. Implement origin validation policy:
   - default bind localhost
   - allowlist configurable
   - if `Origin` is present and invalid, respond HTTP 403
5. Add tests using in-process HTTP server/client.

## Verification
* `ctest --test-dir build`
