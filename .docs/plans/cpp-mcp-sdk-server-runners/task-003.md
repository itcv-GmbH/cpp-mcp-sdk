# Task ID: task-003
# Task Name: Implement STDIO Server Runner (Blocking + Optional Async)

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
  * optional `startAsync()` that runs `run()` on a joinable thread
  * configuration for input/output/error streams and stdio options
* `src/server/stdio_runner.cpp` implements the runner.
* Runner guarantees:
  * only MCP JSON-RPC messages are written to output
  * parse errors emit JSON-RPC parse error response with `id: null` (and do not crash)
  * logs/diagnostics go to error stream only

## Step-by-Step Instructions
1. Implement constructor that stores `ServerFactory` and options; instantiate one `mcp::Server` via factory at run start.
2. Set `server->setOutboundMessageSender(...)` to write serialized JSON-RPC messages to the output stream with `\n` delimiter and flush.
3. In the input loop:
   - read one line (newline-delimited)
   - ignore empty lines
   - parse `jsonrpc::Message` using `mcp::jsonrpc::parseMessage`
   - dispatch:
     - Request: `server->handleRequest(context, request).get()` then write response
     - Notification: `server->handleNotification(context, notification)`
     - Response: `server->handleResponse(context, response)`
4. On parse/dispatch exception:
   - write diagnostics to error stream
   - emit parse error response with `id=null` to output (per JSON-RPC)
5. Implement `stop()` so async mode can terminate (e.g., atomic flag; close input is handled by host).

## Verification
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release`
