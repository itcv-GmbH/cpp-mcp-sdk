# Task ID: [task-019]
# Task Name: [Client Core (connect, initialize/initialized, base RPC calls)]

## Context
Provide the client-side facade that connects over stdio or HTTP, performs lifecycle handshake, and exposes typed RPC calls.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Core MCP Features: Client; Lifecycle; Transport support)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/lifecycle.md`

## Output / Definition of Done
* `include/mcp/client/client.hpp` exposes:
  - connect/attach to transport
  - initialize negotiation config (supported versions)
  - automatic `notifications/initialized`
  - negotiated version/capabilities accessors
* Tests validate ordering rules and error behavior

## Step-by-Step Instructions
1. Implement client construction over stdio and HTTP transports.
2. Implement initialize request with default latest supported version.
3. Implement capability declaration configuration (roots/sampling/elicitation/tasks).
4. Send `notifications/initialized` after successful initialize.
5. Add tests for pre-init request restrictions.

## Verification
* `ctest --test-dir build`
