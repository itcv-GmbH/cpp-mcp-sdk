# Task ID: [task-015]
# Task Name: [Tools (tools/list, tools/call, list_changed)]

## Context
Implement server-side tools: discovery, invocation, schema validation, and list change notifications.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Server Features: Tools)
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/tools.md`
* `.docs/requirements/mcp-spec-2025-11-25/schema/schema.ts` (Tool, CallToolResult)

## Output / Definition of Done
* `include/mcp/server/tools.hpp` defines tool registration API
* `tools/list` supports cursor pagination
* `tools/call` returns `CallToolResult` and uses `isError` for execution failures
* Tool input schema is validated before invocation; `structuredContent` validated if `outputSchema` present
* `notifications/tools/list_changed` emitted when `tools.listChanged` is enabled

## Step-by-Step Instructions
1. Define tool registry data model (name, metadata, schemas, handler).
2. Implement `tools/list` with consistent cursor pagination helper.
3. Implement `tools/call`:
   - validate params and input schema
   - invoke handler
   - validate output schema when present
   - return `isError: true` for tool failures (not protocol errors)
4. Implement optional list-changed notifications.
5. Add tests for schema validation failures vs unknown tool errors.

## Verification
* `ctest --test-dir build`
