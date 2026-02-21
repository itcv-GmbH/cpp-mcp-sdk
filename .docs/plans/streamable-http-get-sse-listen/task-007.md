# Task ID: task-007
# Task Name: Extract Streamable HTTP Client Transport

## Context

`StreamableHttpClientTransport` is currently implemented as a nested class inside `src/client/client.cpp`. This structure couples transport implementation details to the client implementation unit and increases the risk of future feature work introducing unintended dependencies. This task will extract the transport into the `src/transport/` module and will create a dedicated header to define its construction and lifecycle.

## Inputs

* `.docs/requirements/cpp-mcp-sdk.md` sections: "Transport Support" and "Streamable HTTP GET (Server-Initiated Messages)"
* `src/client/client.cpp` (`StreamableHttpClientTransport`)
* `include/mcp/transport/transport.hpp`
* `include/mcp/transport/http.hpp`
* `src/transport/http_client.cpp`

## Output / Definition of Done

* A new transport implementation translation unit will exist under `src/transport/` for the Streamable HTTP client transport.
* A new header will exist under `include/mcp/transport/` that declares a factory function returning `std::shared_ptr<mcp::transport::Transport>` for the Streamable HTTP client transport.
* `src/client/client.cpp` will construct the Streamable HTTP client transport via the new factory function.
* The build will succeed without introducing any new public API types beyond the factory surface.

## Step-by-Step Instructions

1. Create `include/mcp/transport/streamable_http_client_transport.hpp`.
2. In the new header, declare a factory function that constructs the Streamable HTTP client transport using:
   - `mcp::transport::http::StreamableHttpClientOptions`
   - `mcp::transport::http::StreamableHttpClient::RequestExecutor`
   - an inbound message callback of type `std::function<void(const mcp::jsonrpc::Message &)>`.
3. Create `src/transport/streamable_http_client_transport.cpp` and move the `StreamableHttpClientTransport` class definition and implementation into this file.
4. Update `src/client/client.cpp` to include the new header and to call the factory function instead of defining the transport inline.
5. Update `CMakeLists.txt` to include `src/transport/streamable_http_client_transport.cpp` in `MCP_SDK_SOURCES`.
6. Ensure the extracted transport preserves current behavior for POST request round-trips.

## Verification

* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R transport_http_client -V`
