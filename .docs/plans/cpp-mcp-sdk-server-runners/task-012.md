# Task ID: task-012
# Task Name: Implement Combined Runner (Start STDIO, HTTP, or Both)

## Context
Some deployments require a single process to expose an MCP server over both stdio (desktop/local hosts that spawn a subprocess) and Streamable HTTP (remote clients, multi-connection). The SDK already supports both transports, but users must currently orchestrate two runners manually. This task adds a combined runner that composes `StdioServerRunner` and `StreamableHttpServerRunner` and will support starting either or both.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Transport Support: stdio + Streamable HTTP; shutdown expectations)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
* `include/mcp/server/stdio_runner.hpp` (from `task-003`)
* `include/mcp/server/streamable_http_runner.hpp` (from `task-005`)
* `task-002` (ServerFactory contract; session isolation rules)

## Output / Definition of Done
* `include/mcp/server/combined_runner.hpp` added declaring a combined runner, e.g. `mcp::CombinedServerRunner`.
* `src/server/combined_runner.cpp` added implementing the combined runner.
* The combined runner supports:
  * configuring a `ServerFactory`
  * enabling STDIO runner and/or HTTP runner
  * starting HTTP in background (`startHttp()` or `start()`)
  * running STDIO in foreground (`runStdio()` or `run()`)
  * stopping HTTP (`stop()`), with documented stdio stop semantics (stdio exits on EOF; async stop requires host-controlled input stream closure)

## Step-by-Step Instructions
1. Define a small options struct for the combined runner:
   - `bool enableStdio` and `bool enableHttp`
   - `mcp::transport::StdioServerOptions` (or runner-specific stdio options)
   - `mcp::transport::http::StreamableHttpServerOptions` for HTTP
   - stream overrides for stdio input/output/error
2. Implement the combined runner as a composition of the two dedicated runners:
   - construct `StdioServerRunner` and `StreamableHttpServerRunner` lazily based on enabled flags
   - ensure the same `ServerFactory` is used for both transports
3. Provide a minimal API that maps to typical usage:
   - `startHttp()` returns after the HTTP runtime is listening
   - `runStdio()` blocks until EOF
   - `stop()` stops HTTP runtime and unblocks any internal threads owned by the combined runner
4. Document concurrency expectations:
   - calling `startHttp()` then `runStdio()` is the intended “serve both” pattern
   - calling only one transport is required to be supported with the other disabled
5. Ensure any background threads are joinable and are drained on destruction (no detached threads).

## Verification
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release`
