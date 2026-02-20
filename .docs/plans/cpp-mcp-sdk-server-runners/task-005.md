# Task ID: task-005
# Task Name: Implement Streamable HTTP Server Runner (Per-Session Factory + Runtime)

## Context
Serving Streamable HTTP currently requires users to manually wire `StreamableHttpServer` handlers to `mcp::Server` and then separately run `HttpServerRuntime`. This task provides a single runner that owns both, uses a `ServerFactory` to create a new `mcp::Server` per HTTP session, and exposes a simple `start()/stop()` API.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP requirements; session management)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
* `include/mcp/transport/http.hpp`
* `include/mcp/server/server.hpp`
* Existing pattern in `examples/http_server_auth/main.cpp`
* `task-014` (transport issues `MCP-Session-Id` on initialize)

## Output / Definition of Done
* `include/mcp/server/streamable_http_runner.hpp` added declaring (example shape):
  * `class StreamableHttpServerRunner`
  * constructor takes `ServerFactory` and `mcp::transport::http::StreamableHttpServerOptions`
  * `start()`, `stop()`, `isRunning()`, `localPort()`
* `src/server/streamable_http_runner.cpp` implements the runner.
* Server instance routing:
  * when `StreamableHttpServerOptions.http.requireSessionId=true`, on initialize request for a sessionId not yet mapped, create a server via factory and store it under that sessionId; subsequent messages for that sessionId route to the stored server
  * when `StreamableHttpServerOptions.http.requireSessionId=false`, create exactly one server instance on first initialize and route all messages through that server instance
* Outbound sender wiring:
  * for each per-session server instance, set outbound sender to `enqueueServerMessage(message, context.sessionId)`.
* Runner documentation clarifies:
   * multi-client-safe mode requires `StreamableHttpServerOptions.http.requireSessionId=true`
   * when `StreamableHttpServerOptions.http.requireSessionId=false`, the runner is required to treat the process as a single logical session and must document that per-session isolation is not provided

## Step-by-Step Instructions
1. Construct an internal `mcp::transport::http::StreamableHttpServer` with provided options.
2. Construct an internal `mcp::transport::HttpServerRuntime` with `options.http`.
3. Implement request handler:
    - call `streamableServer.handleRequest(request)` only after installing StreamableHttpServer handlers that forward into per-session `mcp::Server` instances.
4. In StreamableHttpServer handler lambdas:
   - when `options.http.requireSessionId=true`, require `context.sessionId` to be present and use it as the session key
   - when `options.http.requireSessionId=false`, use a single fixed session key for all requests
   - if method is `initialize` and no server exists for the selected key, create via factory and store
   - call `server->start()` immediately after creating and before handling any messages
   - forward to `server->handleRequest/Notification/Response`
   - on handler exceptions for requests, return an internal error response with the original request `id`
5. In the runtime request handler wrapper, handle cleanup signals:
   - if request is HTTP DELETE with an `MCP-Session-Id`, remove the mapped server after delegating to `streamableServer.handleRequest`
   - if the transport response is HTTP 404 and the request had `MCP-Session-Id`, remove the mapped server
6. On cleanup that removes a mapped server, call `server->stop()` before erasing it.
7. On `stop()`, stop runtime, call `stop()` on all mapped servers, and clear the session map.

## Verification
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release`
