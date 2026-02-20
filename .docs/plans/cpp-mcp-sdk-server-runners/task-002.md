# Task ID: task-002
# Task Name: Define ServerFactory Contract + Session Isolation Rules

## Context
`mcp::Session` lifecycle state is per connection. Streamable HTTP explicitly supports session management (`MCP-Session-Id`). A runner that serves HTTP traffic must not route multiple sessions through a single `mcp::Server` instance, or initialization/lifecycle state will be shared incorrectly.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP session management)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md` (Streamable HTTP; sessions)
* `include/mcp/jsonrpc/messages.hpp` (`RequestContext.sessionId`)
* `include/mcp/lifecycle/session.hpp` (session state is per instance)

## Output / Definition of Done
* `ServerFactory` contract defined in a public header (or within runner headers), for example:
  * `using ServerFactory = std::function<std::shared_ptr<mcp::Server>()>;`
* Runner behavior rules documented:
  * create a new `mcp::Server` per Streamable HTTP session ID
  * bind outbound sender for each server to route messages via the transport + sessionId
  * define cleanup behavior for expired sessions (e.g., remove server instance when HTTP session is deleted/expired)
* Explicit documentation of single-server vs factory usage, including a helper for “single session only” if supported.

## Step-by-Step Instructions
1. Define `ServerFactory` and whether it is:
   - session-agnostic (no params), or
   - session-aware (accepts sessionId/protocolVersion/authContext).
   Prefer session-aware only if required; otherwise keep it simple.
2. Define how HTTP runner discovers session creation:
   - typically on first accepted `initialize` request where `RequestContext.sessionId` is present.
3. Define how runner handles messages that arrive for unknown sessionId:
   - should not happen if `StreamableHttpServer` validation is authoritative; if it does, treat as internal error.
4. Decide whether stdio runner uses a single `mcp::Server` instance created once via the factory.
5. Document these rules in `include/mcp/server/streamable_http_runner.hpp` comments and in `docs/api_overview.md`.

## Verification
* Review-only: ensure documented rules match the spec and current `Session` lifecycle assumptions.
