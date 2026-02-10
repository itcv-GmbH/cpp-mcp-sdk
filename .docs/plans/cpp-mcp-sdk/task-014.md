# Task ID: [task-014]
# Task Name: [Server Core + Capability Enforcement]

## Context
Provide the server-side facade that binds transports + router + lifecycle and enforces negotiated capabilities before invoking feature handlers.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Core MCP Features: Server; Capability negotiation)
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/index.md`

## Output / Definition of Done
* `include/mcp/server/server.hpp` exposes server configuration and registration entrypoints
* Server rejects unsupported method calls with `-32601` or other spec-appropriate errors
* Pre-init behavior matches lifecycle rules (ping/logging allowed; others rejected/deferred)

## Step-by-Step Instructions
1. Implement server object wiring:
   - lifecycle negotiation (InitializeResult)
   - capability tracking
2. Implement method dispatch table:
   - core methods (initialize, ping)
   - feature methods registered based on configured server capabilities
3. Add enforcement helpers to check negotiated capabilities and return correct JSON-RPC errors.
4. Add tests for calling feature endpoints when capabilities not declared.

## Verification
* `ctest --test-dir build`
