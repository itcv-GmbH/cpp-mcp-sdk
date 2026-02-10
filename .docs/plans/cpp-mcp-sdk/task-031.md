# Task ID: [task-031]
# Task Name: [Conformance: JSON-RPC + Lifecycle + Capability Tests]

## Context
Add automated conformance tests for JSON-RPC invariants and MCP lifecycle rules.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Interoperability/Conformance Testing; JSON-RPC conformance; Lifecycle)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/lifecycle.md`

## Output / Definition of Done
* `tests/conformance/test_jsonrpc_invariants.cpp` validates message rules
* `tests/conformance/test_lifecycle.cpp` validates initialize/initialized ordering
* `tests/conformance/test_capabilities.cpp` validates capability gating and `experimental` passthrough

## Step-by-Step Instructions
1. Write tests for request/notification/response invariants.
2. Write tests that simulate client/server handshake and enforce ordering constraints.
3. Write tests ensuring methods are rejected when capabilities not negotiated.

## Verification
* `ctest --test-dir build -R conformance`
