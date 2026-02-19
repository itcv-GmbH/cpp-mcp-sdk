# Task ID: [task-021]
# Task Name: [Expand Unit Tests: Server Facade]

## Context
Increase server facade tests for capability enforcement, utilities (logging/completion), pagination, and error typing differences (protocol errors vs tool `isError`).

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Server features; server utilities; pagination)
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/tools.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/resources.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/prompts.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/logging.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/completion.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/pagination.md`
* `include/mcp/server/server.hpp`
* `tests/server_test.cpp`

## Output / Definition of Done
* `tests/server_test.cpp` adds tests for:
  - `logging/setLevel` gating and server notification emission level filtering
  - `completion/complete` max 100 values enforcement and correct reference handling
  - pagination cursor opacity and stability across tools/resources/prompts/tasks list endpoints
  - tool failure uses `CallToolResult.isError=true` (not protocol error)
  - resource-not-found uses `-32002`

## Step-by-Step Instructions
1. Extend existing server fixture to register a completion handler and validate returned values and constraints.
2. Add tests for `logging/setLevel` capability gating and ensure notifications respect current level.
3. Add list endpoint tests that validate nextCursor behavior and cursor opacity.
4. Add tool error tests distinguishing tool execution errors vs JSON-RPC errors.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_server_test --output-on-failure`
