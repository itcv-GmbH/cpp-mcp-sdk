# Task ID: task-002
# Task Name: Define ServerFactory Contract + Session Isolation Rules

## Context
`mcp::Session` lifecycle state is per connection. Streamable HTTP explicitly supports session management (`MCP-Session-Id`) and is intended to handle multiple client connections. A runner that serves HTTP traffic must not route multiple sessions through a single `mcp::Server` instance, or initialization/lifecycle state will be shared incorrectly.

For the SDK runners, the multi-client-safe configuration is to require sessions (`StreamableHttpServerOptions.http.requireSessionId=true`) so the server issues `MCP-Session-Id` on initialize and the runner will key server instances by session.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP session management)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md` (Streamable HTTP; sessions)
* `include/mcp/jsonrpc/messages.hpp` (`RequestContext.sessionId`)
* `include/mcp/lifecycle/session.hpp` (session state is per instance)

## Output / Definition of Done
* `ServerFactory` contract defined in a public header (or within runner headers), for example:
  * `using ServerFactory = std::function<std::shared_ptr<mcp::Server>()>;`
* Runner behavior rules documented:
   * STDIO runner uses exactly one server instance.
   * Streamable HTTP runner creates a new server instance per `MCP-Session-Id` when `StreamableHttpServerOptions.http.requireSessionId=true`, and uses exactly one server instance when `StreamableHttpServerOptions.http.requireSessionId=false`.
   * Combined runner creates one server instance for STDIO, and one server instance per HTTP session when `StreamableHttpServerOptions.http.requireSessionId=true`.
* Cleanup rules documented:
   * drop per-session servers on HTTP DELETE
   * drop per-session servers when the transport returns HTTP 404 for that session (expired/terminated)
* Runner lifecycle rules documented:
  * runner must call `mcp::Server::start()` for every server instance it creates before handling any messages
  * runner must call `mcp::Server::stop()` before dropping a server instance due to HTTP DELETE, HTTP 404 cleanup, runner stop, or runner destruction

## Step-by-Step Instructions
1. Define `ServerFactory`:
   - define a simple, session-agnostic factory: `std::function<std::shared_ptr<mcp::Server>()>`.
2. Define when a new HTTP session server is created:
   - on first accepted `initialize` request for a newly issued `MCP-Session-Id`.
3. Define session-keying:
   - when `StreamableHttpServerOptions.http.requireSessionId=true`, use `RequestContext.sessionId` as the key and treat missing `sessionId` as an internal error
   - when `StreamableHttpServerOptions.http.requireSessionId=false`, use a single fixed key for all requests and treat `RequestContext.sessionId` as `std::nullopt`
4. Define cleanup triggers:
   - on HTTP DELETE for a sessionId
   - on HTTP 404 responses for a sessionId
5. Document these rules in `include/mcp/server/streamable_http_runner.hpp` and in `docs/api_overview.md`.

## Verification
* Review-only: ensure documented rules match the spec and current `Session` lifecycle assumptions.
