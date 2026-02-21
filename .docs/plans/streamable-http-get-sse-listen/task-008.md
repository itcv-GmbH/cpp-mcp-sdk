# Task ID: task-008
# Task Name: Introduce Shared HTTP Header State

## Context

Streamable HTTP requires that clients replay `MCP-Session-Id` and `MCP-Protocol-Version` headers consistently across subsequent requests, including both POST requests and GET SSE listen requests. The current `StreamableHttpClientOptions` value-owns `SessionHeaderState` and `ProtocolVersionHeaderState`. A GET listen loop plus potential future multi-channel HTTP usage require a shared, synchronized state so that all HTTP channels will use a consistent view of session and protocol negotiation.

## Inputs

* `.docs/requirements/cpp-mcp-sdk.md` sections: "Streamable HTTP Session Management" and "Streamable HTTP Protocol Version Header"
* `include/mcp/transport/http.hpp` (`SessionHeaderState`, `ProtocolVersionHeaderState`, `StreamableHttpClientOptions`, `HttpClientOptions`)
* `src/transport/http_client.cpp`
* `src/client/client.cpp`

## Output / Definition of Done

* `include/mcp/transport/http.hpp` will define a shared header state type that owns:
  - session header state (`MCP-Session-Id`)
  - protocol version header state (`MCP-Protocol-Version`)
  - synchronization required for concurrent access
* `StreamableHttpClientOptions` will reference the shared header state by `std::shared_ptr<...>`.
* `HttpClientOptions` will reference the shared header state by `std::shared_ptr<...>`.
* `src/transport/http_client.cpp` will read and update header state exclusively through the shared object.
* Existing call sites will be updated to construct and pass a shared header state instance.

## Step-by-Step Instructions

1. Define a new `mcp::transport::http::SharedHeaderState` type in `include/mcp/transport/http.hpp`.
2. Move `SessionHeaderState` and `ProtocolVersionHeaderState` storage into `SharedHeaderState`.
3. Update `StreamableHttpClientOptions` to store `std::shared_ptr<SharedHeaderState>`.
4. Update `HttpClientOptions` to store `std::shared_ptr<SharedHeaderState>`.
5. Update `src/transport/http_client.cpp` to:
   - replay headers via `SharedHeaderState`
   - capture session headers from initialize responses via `SharedHeaderState`
6. Update `src/client/client.cpp` to create and propagate a shared header state instance across the HTTP runtime and Streamable HTTP client.
7. Update tests that construct options directly to pass a shared header state instance.

## Verification

* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R transport_http_client -V`
