# Task ID: [task-007]
# Task Name: [Implement Schema Validation Module (Pinned MCP schema.json)]

## Context
Enable runtime validation of MCP messages and tool schemas against the pinned MCP JSON Schema (2020-12), per SRS.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Schema Validation; Normative Conformance)
* `.docs/requirements/mcp-spec-2025-11-25/schema/schema.json`

## Output / Definition of Done
* `include/mcp/schema/validator.hpp` exposes:
  - load pinned MCP schema
  - validate an instance against schema
  - validate a schema object for tool input/output schemas
* Tests demonstrate:
  - a valid `initialize` request passes
  - an invalid message fails with structured diagnostics
  - schemas without `$schema` default to draft 2020-12 (as required)

## Step-by-Step Instructions
1. Add a vendoring/pinning mechanism to ensure the exact `schema.json` used by the SDK is traceable (URL + commit/tag recorded in docs).
2. Implement schema validator wrapper around `jsoncons` JSON Schema support.
3. Support dialect selection:
   - if `$schema` absent, assume 2020-12
   - unsupported dialect -> graceful error
4. Integrate validator into JSON-RPC parse pipeline at the MCP layer (method-specific validation).
5. Add tests for dialect handling, invalid schema, and invalid instances.

## Verification
* `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
* `cmake --build build`
* `ctest --test-dir build`
