# Task ID: [task-021]
# Task Name: [Client: Roots (handle roots/list + roots/list_changed)]

## Context
Support roots capability and server-initiated `roots/list` requests for filesystem root discovery.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Client Features: Roots)
* `.docs/requirements/mcp-spec-2025-11-25/spec/client/roots.md`

## Output / Definition of Done
* `include/mcp/client/roots.hpp` defines a roots provider interface
* Client handles inbound `roots/list` with correct errors when unsupported
* If `roots.listChanged` enabled, client can send `notifications/roots/list_changed`
* Root URIs are `file://` per spec

## Step-by-Step Instructions
1. Define a roots provider callback returning a list of root entries.
2. Register inbound handler for `roots/list` in the client router.
3. Implement `notifications/roots/list_changed` sender.
4. Add tests for capability gating and URI validation.

## Verification
* `ctest --test-dir build`
