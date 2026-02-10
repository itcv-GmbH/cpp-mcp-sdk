# Task ID: [task-004]
# Task Name: [Implement JSON-RPC Message Model + Parsing/Encoding]

## Context
Implement strict JSON-RPC 2.0 handling and UTF-8 JSON parsing/serialization as the foundation for all MCP methods.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (JSON-RPC Message Conformance)
* `.docs/requirements/mcp-spec-2025-11-25/schema/schema.ts` (JSONRPCMessage types)

## Output / Definition of Done
* `include/mcp/jsonrpc/messages.hpp` defines request/notification/response structs
* `src/jsonrpc/messages.cpp` implements parse/serialize with strict invariants
* Unit tests cover:
  - request `id` non-null and unique per sender (enforced at router level)
  - notification has no `id`
  - response has exactly one of `result` or `error`

## Step-by-Step Instructions
1. Define `RequestId` type supporting integer or string IDs.
2. Implement strict parsing:
   - reject invalid `jsonrpc` values
   - enforce notification vs request shapes
   - preserve unknown fields when schema allows additional properties
3. Implement encoding:
   - always emit UTF-8 JSON
   - provide a “no embedded newlines” encoder option for stdio transport
4. Implement JSON-RPC error object modeling (code/message/data) and helpers for standard errors.
5. Add tests for malformed inputs and correct error response behavior (including unknown `id` cases).

## Verification
* `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
* `cmake --build build`
* `ctest --test-dir build`
