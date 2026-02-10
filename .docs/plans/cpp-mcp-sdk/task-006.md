# Task ID: [task-006]
# Task Name: [Implement Lifecycle + Version/Capability Negotiation]

## Context
Implement MCP lifecycle phases and enforce ordering rules (initialize first; initialized notification; capability gating).

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Lifecycle Management; Versioning and Negotiation)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/lifecycle.md`
* `.docs/requirements/mcp-spec-2025-11-25/schema/schema.ts` (InitializeRequest/Result shapes)

## Output / Definition of Done
* `include/mcp/lifecycle/session.hpp` defines session states and negotiated parameters
* Client enforces:
  - `initialize` is first request
  - only `ping` allowed before initialize response
  - `notifications/initialized` sent after initialize success
* Server enforces:
  - rejects/defers invalid pre-init requests (except ping + allowed logging)
  - does not send feature requests pre-initialized

## Step-by-Step Instructions
1. Implement a session state machine shared by client/server roles.
2. Implement version negotiation:
   - client proposes preferred version (latest supported by default)
   - server selects supported version (latest supported preferred)
   - expose negotiated version
3. Implement capability negotiation objects with `experimental` passthrough.
4. Add enforcement hooks so feature handlers can check negotiated capabilities.
5. Add tests for lifecycle ordering violations and actionable errors on negotiation failure.

## Verification
* `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
* `cmake --build build`
* `ctest --test-dir build`
