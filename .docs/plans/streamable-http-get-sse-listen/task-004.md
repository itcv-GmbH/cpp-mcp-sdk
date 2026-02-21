# Task ID: task-004
# Task Name: Integrate GET Listen Loop Into StreamableHttpClientTransport

## Context

`mcp::Client` already implements handlers for server-initiated requests (`roots/list`, `sampling/createMessage`, `elicitation/create`). Over HTTP, these requests will arrive via a GET-opened SSE stream. The current `StreamableHttpClientTransport` only processes inbound messages that are returned as part of POST requests. This task will integrate a GET listen loop into the transport.

## Inputs

* `.docs/requirements/cpp-mcp-sdk.md` section: "Streamable HTTP GET (Server-Initiated Messages)"
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md` section: "Listening for Messages from the Server"
* `include/mcp/transport/streamable_http_client_transport.hpp`
* `src/transport/streamable_http_client_transport.cpp`
* `src/client/client.cpp`
* `src/transport/http_client.cpp` (`openListenStream`, `pollListenStream`)

## Output / Definition of Done

* `StreamableHttpClientTransport` will start a background GET listen loop after a successful `initialize` request.
* The listen loop will dispatch inbound JSON-RPC messages to the transport's inbound message handler.
* The listen loop will terminate cleanly on `Client::stop()` and on transport destruction.
* The implementation will handle HTTP 405 from GET by disabling server-initiated listening without failing POST functionality.

## Step-by-Step Instructions

1. Extend `StreamableHttpClientTransport` to store:
   - a background thread
   - an atomic running flag for the listen loop
   - a state flag indicating whether GET listening is enabled by configuration
2. Update `StreamableHttpClientTransport::send` to detect a successful `initialize` request completion and to start the listen loop by calling `client_.openListenStream()`.
3. Implement the background loop to repeatedly:
   - call `client_.pollListenStream()` while the stream is open
   - dispatch every returned message via the inbound message handler
   - stop when the stream closes or when the transport stops
4. Implement stop semantics so that:
   - `stop()` signals the listen loop to terminate
   - `stop()` joins the background thread
   - repeated `start()`/`stop()` cycles remain safe
5. Ensure that listen-loop failures do not crash the process. Failures must result in listen termination and must keep POST send operational.

## Verification

* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R transport_http_client -V`
