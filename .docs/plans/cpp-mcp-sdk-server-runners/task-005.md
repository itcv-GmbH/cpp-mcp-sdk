# Task ID: task-005
# Task Name: Implement Streamable HTTP Server Runner (Factory + Runtime)

## Context
Serving Streamable HTTP currently requires users to manually wire `StreamableHttpServer` handlers to `mcp::Server` and then separately run `HttpServerRuntime`. This task provides a single runner that owns both, uses a `ServerFactory` for per-session isolation, and exposes a simple `start()/stop()` API.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP requirements; session management)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
* `include/mcp/transport/http.hpp`
* `include/mcp/server/server.hpp`
* Existing pattern in `examples/http_server_auth/main.cpp`

## Output / Definition of Done
* `include/mcp/server/streamable_http_runner.hpp` added declaring (example shape):
  * `class StreamableHttpServerRunner`
  * constructor takes `ServerFactory` and `mcp::transport::http::StreamableHttpServerOptions`
  * `start()`, `stop()`, `isRunning()`, `localPort()`
* `src/server/streamable_http_runner.cpp` implements the runner.
* Per-session server map:
  * on initialize request for a sessionId not yet mapped, create server via factory and store
  * route subsequent messages for that sessionId to that server
* Outbound sender wiring:
  * for each per-session server instance, set outbound sender to `enqueueServerMessage(message, context.sessionId)`.

## Step-by-Step Instructions
1. Construct an internal `mcp::transport::http::StreamableHttpServer` with provided options.
2. Construct an internal `mcp::transport::HttpServerRuntime` with `options.http`.
3. Implement request handler:
   - call `streamableServer.handleRequest(request)` only after installing StreamableHttpServer handlers that forward into per-session `mcp::Server` instances.
4. In StreamableHttpServer handler lambdas:
   - look up `context.sessionId` (required for HTTP sessions)
   - if method is `initialize` and no server exists for sessionId, create via factory and store
   - forward to `server->handleRequest/Notification/Response`
5. On `stop()`, stop runtime and clear session map.

## Verification
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release`
