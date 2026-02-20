# Plan Review Notes (Consistency + Plausibility)

## Key Findings

1. True multi-client Streamable HTTP requires server-issued `MCP-Session-Id`.
   - The MCP spec assigns the session ID at initialization time via the HTTP response containing `InitializeResult`.
   - To support per-session `mcp::Server` instances, the transport must issue session IDs and bind them into `RequestContext.sessionId` for initialize.

2. STDIO parse-error responses must serialize `"id": null`.
   - In this codebase, that requires `mcp::jsonrpc::ErrorResponse.hasUnknownId = true` (or using `mcp::jsonrpc::makeUnknownIdErrorResponse(...)`).

3. Test naming in the tasks must align with existing CMake conventions.
   - Existing pattern: `add_executable(mcp_sdk_test_<thing> ...)` plus `add_test(NAME mcp_sdk_<thing>_test COMMAND mcp_sdk_test_<thing>)`.

4. Runners must manage `mcp::Server` lifecycle.
   - Every runner-created `mcp::Server` instance must have `start()` called before message handling and must have `stop()` called before it is dropped.

5. Streamable HTTP runner behavior must be defined for `requireSessionId=false`.
   - The runner must route all traffic through exactly one server instance and must document that per-session isolation is not provided.

6. Integration coverage must validate runner behavior using the pinned Python reference SDK.
   - The integration fixture under `tests/integration/` must migrate to the Streamable HTTP runner and must assert server-issued `MCP-Session-Id` behavior on authenticated initialize.
   - The integration suite must add Python reference client coverage for the STDIO runner by spawning a C++ stdio server fixture and exercising end-to-end protocol flow.

## Changes Applied To This Plan

This plan is updated to be spec-aligned for multi-client Streamable HTTP:

- Added transport work (`task-014`, `task-015`) so `StreamableHttpServer` issues `MCP-Session-Id` on successful initialize when `requireSessionId=true`.
- Streamable HTTP runner design uses per-session isolation (one `mcp::Server` per `MCP-Session-Id`).
- Task-level test naming matches the repository's `mcp_sdk_test_*` / `mcp_sdk_*_test` convention.
- STDIO runner tasks call out the `hasUnknownId` mechanism required for `"id": null`.

## Follow-Ups (Not In This Plan)

None.
