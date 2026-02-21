# Task ID: task-009
# Task Name: Introduce Unified Transport Inbound Loop Abstraction

## Context

Both stdio and Streamable HTTP client transports must run background activity to receive inbound JSON-RPC messages and to dispatch them into `mcp::Client::handleMessage`. The stdio transport already uses a reader thread. The Streamable HTTP transport will add a GET SSE listen loop. These loops must share a single lifecycle model that will guarantee consistent shutdown, join, and error containment.

## Inputs

* `include/mcp/transport/transport.hpp`
* `src/client/client.cpp` (`SubprocessStdioClientTransport`)
* `src/transport/streamable_http_client_transport.cpp` (after `task-007`)
* `include/mcp/security/limits.hpp` (retry and duration limits)

## Output / Definition of Done

* A new internal helper abstraction will exist under `include/mcp/detail/` or `src/detail/` that defines:
  - a start function
  - a stop function
  - a join function
  - an error containment strategy that prevents exceptions escaping the thread boundary
* `SubprocessStdioClientTransport` will use this helper for its reader thread lifecycle.
* `StreamableHttpClientTransport` will use this helper for its GET SSE listen loop lifecycle.
* Unit tests will validate that `Client::stop()` will terminate both transports without hangs.

## Step-by-Step Instructions

1. Define an internal helper type `mcp::detail::InboundLoop` that owns:
   - a thread
   - an atomic run flag
   - a callable loop body
2. Define required operations:
   - `start()` that spawns the thread and sets run state
   - `stop()` that clears the run flag
   - `join()` that joins the thread
3. Update `SubprocessStdioClientTransport` to use `InboundLoop` for `readerThread_` ownership and lifecycle.
4. Update `StreamableHttpClientTransport` to use `InboundLoop` for the GET listen loop thread ownership and lifecycle.
5. Add or update tests to assert that repeated start-stop cycles do not leak threads and do not hang.

## Verification

* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R client -V`
