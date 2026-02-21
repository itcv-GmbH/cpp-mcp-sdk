# Streamable HTTP GET SSE Listen - Implementation Plan

## Architecture Decision Record (ADR)

- The SDK will support server-initiated JSON-RPC requests and notifications over Streamable HTTP by opening an SSE stream via HTTP GET to the MCP endpoint.
- The HTTP client transport used by `mcp::Client` will run a background listen loop that will:
  - open a GET SSE stream after the HTTP session is established via `initialize`
  - poll the stream using `Last-Event-ID` resumption
  - respect SSE `retry` guidance by delaying reconnect attempts
  - dispatch inbound JSON-RPC messages into `mcp::Client::handleMessage`.
- `mcp::transport::http::StreamableHttpClient` will be made thread-safe for concurrent POST traffic (`send`) and GET listen traffic (`openListenStream`/`pollListenStream`).
- The SDK will default to a real delay for SSE `retry` handling. Tests will override the delay via injected hooks to avoid wall-clock sleeps.

## Target Files

- `.docs/requirements/cpp-mcp-sdk.md`
- `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
- `include/mcp/transport/http.hpp`
- `src/transport/http_client.cpp`
- `src/client/client.cpp`
- `tests/transport_http_client_test.cpp`
- `tests/client_test.cpp` or a new test file under `tests/`
- `examples/` (add or update at least one example demonstrating server-initiated message handling over HTTP)

## Verification Strategy

- Unit tests will validate Streamable HTTP client behaviors:
  - GET listen open and poll behavior
  - `Last-Event-ID` resumption behavior
  - `retry` delay handling behavior
  - concurrency safety between POST and GET operations
- Integration-style tests will validate that a server-initiated request delivered via GET SSE is handled by `mcp::Client` and that the corresponding JSON-RPC response is posted back to the server.
- The implementation will be verified by running:
  - `cmake --preset vcpkg-unix-release`
  - `cmake --build build/vcpkg-unix-release`
  - `ctest --test-dir build/vcpkg-unix-release`

## Scope Guardrails

- This plan will not change the public JSON-RPC message model types.
- This plan will not add HTTP connection pooling.
- This plan will not add a new transport interface beyond Streamable HTTP and stdio.
- This plan will not change server-side Streamable HTTP behavior except where tests require adjustments.
- This plan will not implement automatic session re-initialization on HTTP 404. The SDK will expose the failure signal, and the application will be responsible for re-initializing.

## Risks / Unknowns

- `mcp::transport::http::StreamableHttpClient` currently holds mutable state without synchronization. Adding a listen loop will introduce concurrent access and will require correct locking to avoid races and deadlocks.
- Default `retry` delays will introduce wall-clock waiting. Tests must override delay behavior to keep the suite fast and deterministic.
- Some servers will respond with HTTP 405 for GET on the MCP endpoint. The client listen loop must treat 405 as a supported configuration and must remain functional for POST-only operation.
- Some servers will require `MCP-Session-Id` for GET. The transport must not attempt a GET listen stream until after `initialize` completes and the session header state is captured.
- Failure modes (HTTP 404 session expiration, invalid `Last-Event-ID`, transient HTTP errors) will require a consistent strategy for surfacing errors to callers without crashing background threads.

## Architecture Improvement Opportunities (Not Implemented By This Plan)

- The nested `StreamableHttpClientTransport` in `src/client/client.cpp` will benefit from extraction into `src/transport/` with a dedicated header to improve compilation boundaries and maintainability.
- `SessionHeaderState` and `ProtocolVersionHeaderState` are value-owned in `StreamableHttpClientOptions` and will benefit from shared ownership to enable multiple coordinated HTTP channels.
- Transport lifecycle and inbound dispatch will benefit from a unified event-loop abstraction across stdio and HTTP transports.
