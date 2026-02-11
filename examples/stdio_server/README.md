# stdio_server

Minimal MCP server over stdio demonstrating:
- tools (`tools/list`, `tools/call`)
- resources (`resources/list`, `resources/read`)
- prompts (`prompts/list`, `prompts/get`)
- task-capable tools (`params.task` support on `tools/call`)

## Build

```bash
cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON
cmake --build build/vcpkg-unix-release --target mcp_sdk_example_stdio_server
```

## Run

```bash
./build/vcpkg-unix-release/examples/stdio_server/mcp_sdk_example_stdio_server
```

The server reads newline-delimited JSON-RPC messages from `stdin` and writes responses to `stdout`.

## Quick manual smoke test

In another terminal, pipe initialize + initialized + tools/list:

```bash
printf '%s\n' \
'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"manual-client","version":"0.1.0"}}}' \
'{"jsonrpc":"2.0","method":"notifications/initialized"}' \
'{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'
```
