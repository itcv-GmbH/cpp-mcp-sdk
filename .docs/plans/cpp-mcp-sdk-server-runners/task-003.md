# Task ID: task-003
# Task Name: Implement STDIO Server Runner (Blocking + Async)

## Context
The SRS requires APIs to run a server over stdio (read stdin, write stdout). Today users must implement a loop manually or drop down to `jsonrpc::Router` APIs. This task adds a convenience runner that uses `mcp::Server` directly.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (stdio requirements; “must not write to stdout”) 
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md` (stdio transport)
* `include/mcp/server/server.hpp`
* `include/mcp/jsonrpc/messages.hpp` (parse/serialize)
* Existing patterns in `examples/stdio_server/main.cpp`

## Output / Definition of Done
* `include/mcp/server/stdio_runner.hpp` added declaring (example shape):
   * `class StdioServerRunner` constructed with `ServerFactory` (from `task-002`)
   * `run()` that blocks until EOF or `stop()`
   * `startAsync()` that runs `run()` on a joinable thread
   * configuration for input/output/error streams and stdio options
* `src/server/stdio_runner.cpp` implements the runner.
* Runner guarantees:
   * only MCP JSON-RPC messages are written to output
   * parse errors emit JSON-RPC parse error response with `"id": null` (use `ErrorResponse.hasUnknownId=true` / `makeUnknownIdErrorResponse(...)`)
   * request dispatch exceptions emit JSON-RPC internal error responses with the original request `id`
   * logs/diagnostics go to error stream only
   * the runner calls `server->start()` before processing messages and calls `server->stop()` before returning from `run()`

## Step-by-Step Instructions
1. Implement constructor that stores `ServerFactory` and options; instantiate one `mcp::Server` via factory at run start and call `server->start()`.
2. Set `server->setOutboundMessageSender(...)` to write serialized JSON-RPC messages to the output stream with `\n` delimiter and flush.
   - Serialize with `mcp::jsonrpc::EncodeOptions{.disallowEmbeddedNewlines=true}` to guarantee stdio framing.
3. In the input loop:
   - read one line (newline-delimited)
   - reject an unterminated EOF frame (line read with `input.eof()` set)
   - ignore empty lines
   - reject lines that exceed `options.limits.maxMessageSizeBytes` before parsing
   - parse `jsonrpc::Message` using `mcp::jsonrpc::parseMessage`
   - dispatch:
     - Request: `server->handleRequest(context, request).get()` then write response
     - Notification: `server->handleNotification(context, notification)`
     - Response: `server->handleResponse(context, response)`
4. On parse exceptions:
   - write diagnostics to error stream
   - emit parse error response with unknown id to output (per JSON-RPC): `ErrorResponse.hasUnknownId=true` so it serializes as `"id": null`
5. On request dispatch exceptions:
   - write diagnostics to error stream
   - emit internal error response with the original request `id`
6. On notification/response dispatch exceptions:
   - write diagnostics to error stream
   - do not emit any output
7. Implement `stop()` so async mode will terminate (atomic flag); the host is responsible for closing the input stream to unblock a blocking read.
8. Call `server->stop()` on exit paths from `run()`.

## Verification
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release`
