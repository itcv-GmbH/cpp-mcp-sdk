# dual_transport_server

An advanced example demonstrating how to run a single MCP server logic core exposed over **multiple transports simultaneously**.

This example runs a single `mcp::server::Server` instance that can be accessed via:
1. **stdio** (Standard Input/Output, for local process connections)
2. **Streamable HTTP** (HTTP POST/GET SSE, for remote or decoupled clients)

## Architecture & Code Flow

1. **Server Factory**: The example defines a factory method (`makeServer()`) to instantiate the core MCP server logic with its registered tools, resources, and prompts.
2. **Multi-Threading**: Because `stdio` uses a blocking `read()` loop on `stdin` and HTTP requires its own Boost.Asio event loop, they cannot easily share the same thread.
3. **CombinedRunner**: The SDK provides a `CombinedRunner` which takes multiple runner configurations (in this case, `StdioServerRunnerOptions` and `StreamableHttpServerRunnerOptions`) and multiplexes them securely. It spins up the necessary background threads to run both transports concurrently while proxying their requests to the shared `mcp::server::Server` instance.
4. **Signal Handling**: Demonstrates graceful shutdown by trapping `SIGINT`/`SIGTERM` and correctly shutting down both the HTTP server and stdio transport.

## Build

```bash
cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build/vcpkg-unix-release --target mcp_sdk_example_dual_transport_server
```

## Run

```bash
./build/vcpkg-unix-release/examples/dual_transport_server/mcp_sdk_example_dual_transport_server
```

By default, the HTTP transport will bind to `127.0.0.1:8080/mcp`.

## Testing Both Transports

While the server is running, you can interact with it via `stdio` directly in the terminal where it is running, AND via HTTP from a separate terminal.

**1. Interact via stdio (in the same terminal):**
```bash
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"stdio-client","version":"1.0.0"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}
```

**2. Interact via HTTP (in a new terminal):**
```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"http-client","version":"1.0.0"}}}'
```