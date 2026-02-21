# Task ID: task-005
# Task Name: Add End-to-End Test For Server-Initiated Requests Over HTTP

## Context

The SDK must demonstrate that server-initiated JSON-RPC requests delivered over a GET SSE stream are handled by `mcp::Client` and that the generated JSON-RPC response is posted back to the server. This task will add a deterministic test using an in-process Streamable HTTP server and the `Client::connectHttp` overload that accepts a request executor.

## Inputs

* `.docs/requirements/cpp-mcp-sdk.md` section: "Handling server-initiated requests"
* `src/client/client.cpp` (server-initiated request handlers and transport wiring)
* `include/mcp/transport/http.hpp` (`StreamableHttpServer`, `StreamableHttpClient`)
* Existing tests:
  - `tests/transport_http_client_test.cpp`
  - `tests/server_streamable_http_runner_test.cpp`

## Output / Definition of Done

* A new test will assert the following flow:
  - client initializes over Streamable HTTP
  - client sends `notifications/initialized` and opens and polls GET listen stream
  - server enqueues a `roots/list` request message for the session
  - client receives the request, calls the configured roots provider, and posts a valid JSON-RPC response
  - server receives the response and validates the `roots/list` result schema
* The test will pass under `ctest --test-dir build/vcpkg-unix-release`.

## Step-by-Step Instructions

1. Create an in-process `mcp::transport::http::StreamableHttpServer` instance in a test.
2. Implement server handlers for:
   - request handling (initialize and any required lifecycle messages)
   - response handling (capture responses posted back by the client)
3. Create an `mcp::Client` and configure a roots provider that returns at least one `file://` root.
4. Connect the client using `Client::connectHttp(StreamableHttpClientOptions, RequestExecutor)` where the executor delegates to the in-process server.
5. Run the initialize lifecycle (`initialize` and `notifications/initialized`).
6. Enqueue a `roots/list` request from the server using `StreamableHttpServer::enqueueServerMessage` with the active session ID.
7. Assert that the server receives a valid JSON-RPC response containing a roots list.

## Verification

* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R client -V`
