# Task ID: [task-020]
# Task Name: [Client: Tools/Resources/Prompts APIs]

## Context
Implement client-side convenience APIs for the core server features and ensure schema validation and pagination behavior is consistent.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Core MCP Features: Client)
* `.docs/requirements/mcp-spec-2025-11-25/schema/schema.ts`

## Output / Definition of Done
* Client APIs for:
  - `tools/list`, `tools/call`
  - `resources/list`, `resources/read`, `resources/templates/list`
  - `prompts/list`, `prompts/get`
* Pagination cursor helpers for list endpoints
* Tests cover capability gating and basic round-trips with local server

## Step-by-Step Instructions
1. Add typed wrappers for each RPC method.
2. Ensure schema validation of responses where appropriate.
3. Implement cursor iteration helper utilities.
4. Add integration tests against an in-process SDK server.

## Verification
* `ctest --test-dir build`
