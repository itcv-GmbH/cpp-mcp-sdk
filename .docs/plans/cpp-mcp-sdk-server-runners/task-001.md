# Task ID: task-001
# Task Name: Define Runner Public API + Docs Updates

## Context
The SDK meets protocol requirements, but server implementers currently must hand-wire STDIO loops and Streamable HTTP runtime plumbing (see `examples/stdio_server/main.cpp` and `examples/http_server_auth/main.cpp`). This task defines a stable, minimal public API for runners so users can run servers over STDIO and HTTP with a small amount of code.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Transport Support; Documentation requirements)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
* `docs/api_overview.md`
* `examples/stdio_server/main.cpp`
* `examples/http_server_auth/main.cpp`

## Output / Definition of Done
* New public header paths and names finalized:
  * `include/mcp/server/runners.hpp`
  * `include/mcp/server/stdio_runner.hpp`
  * `include/mcp/server/streamable_http_runner.hpp`
  * `include/mcp/server/combined_runner.hpp`
* Public API signatures finalized (documented in header comments and `docs/api_overview.md`).
* `docs/api_overview.md` updated with a new section describing server runners and how they relate to existing transport primitives.

## Step-by-Step Instructions
1. Decide whether runners live in `namespace mcp` (preferred for consistency with `mcp::Server`) or a nested namespace; document the decision.
2. Define the minimal surface for STDIO:
   - a blocking run API (`run()`)
   - a configurable input/output/error stream API (defaults to `std::cin/std::cout/std::cerr`)
   - options passthrough to `mcp::transport::StdioServerOptions` and/or `mcp::transport::StdioAttachOptions`
3. Define the minimal surface for Streamable HTTP:
   - a `start()`/`stop()` API that owns an `mcp::transport::HttpServerRuntime`
   - options passthrough to `mcp::transport::http::StreamableHttpServerOptions`
   - accessors like `isRunning()` and `localPort()` to support quickstarts
4. Ensure API design explicitly addresses per-session isolation (delegated to `task-002`).
5. Define the minimal surface for the combined runner:
   - a single class that can enable STDIO, HTTP, or both
   - `startHttp()`, `runStdio()`, and `stop()` (or a single `start()` that starts configured transports)
   - preserves the ability to use `StdioServerRunner` and `StreamableHttpServerRunner` directly
6. Update `docs/api_overview.md` to include:
   - intended usage snippets (no full examples)
   - how runners avoid writing logs to STDOUT for stdio

## Verification
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release` (compile-time verification once headers exist)
