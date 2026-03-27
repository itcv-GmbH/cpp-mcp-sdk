# stdio_server

A minimal, self-contained MCP server that communicates over Standard Input/Output (stdio). This is the most common transport mechanism for local MCP tools.

This example demonstrates how to set up and expose core MCP capabilities:
- **Tools** (`tools/list`, `tools/call`): Exposes an `echo` tool and a `delayed_echo` tool.
- **Resources** (`resources/list`, `resources/read`): Exposes a static text resource.
- **Prompts** (`prompts/list`, `prompts/get`): Exposes a prompt template with arguments.
- **Tasks** (`params.task`): Demonstrates how tools can report progress and be canceled asynchronously (seen in `delayed_echo`).

## Architecture & Code Flow

1. **Server Initialization**: The server is created using `mcp::server::Server::create()` and configured with specific capabilities (Tools, Resources, Prompts, Tasks).
2. **Registration**: Tools, resources, and prompts are registered with callback lambda functions that execute when the client requests them.
3. **Transport Attachment**: A `mcp::transport::StdioTransport` is created and attached to the server.
4. **Event Loop**: The server runs a blocking event loop reading newline-delimited JSON-RPC messages from `stdin` and writing responses to `stdout`.

## Build

```bash
cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build/vcpkg-unix-release --target mcp_sdk_example_stdio_server
```

## Run

```bash
./build/vcpkg-unix-release/examples/stdio_server/mcp_sdk_example_stdio_server
```

## Quick manual smoke test

Because this uses stdio, you can interact with it directly in your terminal. In another terminal, pipe the `initialize`, `notifications/initialized`, and `tools/list` JSON-RPC messages into the server:

```bash
printf '%s\n' \
'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"manual-client","version":"0.1.0"}}}' \
'{"jsonrpc":"2.0","method":"notifications/initialized"}' \
'{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' | ./build/vcpkg-unix-release/examples/stdio_server/mcp_sdk_example_stdio_server
```