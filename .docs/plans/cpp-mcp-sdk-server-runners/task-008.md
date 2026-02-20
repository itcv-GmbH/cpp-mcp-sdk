# Task ID: task-008
# Task Name: Migrate Existing Examples to Runners

## Context
Examples are part of the SRS documentation deliverable. Migrating them to runners demonstrates the intended easy-path and reduces copy/paste boilerplate.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Documentation requirements)
* `examples/stdio_server/main.cpp`
* `examples/http_server_auth/main.cpp`
* Runner headers implemented in `task-003` and `task-005`

## Output / Definition of Done
* `examples/stdio_server/main.cpp` updated to use `StdioServerRunner`.
* `examples/http_server_auth/main.cpp` updated to use `StreamableHttpServerRunner` while preserving OAuth/bearer auth demonstration and enabling session IDs (`requireSessionId=true`).
* Example behavior unchanged from user perspective.

## Step-by-Step Instructions
1. Refactor `examples/stdio_server/main.cpp`:
   - keep tool/resource/prompt registrations
   - replace manual stdin loop with `StdioServerRunner`
2. Refactor `examples/http_server_auth/main.cpp`:
    - keep auth configuration in `StreamableHttpServerOptions`
    - replace manual wiring of handlers/runtime with HTTP runner
    - enable `StreamableHttpServerOptions.http.requireSessionId = true` so initialize returns `MCP-Session-Id` and subsequent requests require it
3. Ensure all logging stays on stderr for stdio example.

## Verification
* Build and run both examples per `docs/quickstart_server.md`.
