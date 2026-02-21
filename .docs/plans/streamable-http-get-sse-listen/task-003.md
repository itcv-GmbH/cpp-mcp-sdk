# Task ID: task-003
# Task Name: Make StreamableHttpClient Thread-Safe

## Context

Enabling a background GET listen loop will run concurrently with POST requests and SSE resumption logic. `StreamableHttpClient` currently maintains mutable state (`listenState`, session/protocol header state, legacy fallback state) without synchronization. This task will introduce the required synchronization for safe concurrent use.

## Inputs

* `.docs/requirements/cpp-mcp-sdk.md` sections: "Streamable HTTP SSE Semantics" and "Streamable HTTP GET (Server-Initiated Messages)"
* `src/transport/http_client.cpp`
* `include/mcp/transport/http.hpp` (`StreamableHttpClient`, `StreamableHttpClientOptions`, header state types)
* Existing tests: `tests/transport_http_client_test.cpp`

## Output / Definition of Done

* `mcp::transport::http::StreamableHttpClient` will support concurrent invocation of:
  - `send`
  - `openListenStream`
  - `pollListenStream`
* The implementation will not hold internal locks while waiting for reconnect delays.
* A new unit test will exercise concurrent `send` and `pollListenStream` without data races or crashes.

## Step-by-Step Instructions

1. Add an internal mutex to `StreamableHttpClient::Impl` in `src/transport/http_client.cpp`.
2. Guard all shared mutable state reads and writes with the mutex.
3. Restructure `pollListenStream` so that delay waiting occurs outside of the mutex.
4. Ensure request execution via `requestExecutor` is protected from concurrent use when it is not thread-safe by serializing calls under the mutex.
5. Add a unit test that spawns two threads:
   - one that issues repeated `send` calls with small JSON-RPC notifications
   - one that issues repeated listen polling against a stubbed request executor
   The test must assert successful completion and must run within a bounded time.

## Verification

* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R transport_http_client -V`
