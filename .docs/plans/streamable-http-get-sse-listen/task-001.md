# Task ID: task-001
# Task Name: Define GET Listen Configuration Surface

## Context

This task will define the configuration surface required to enable and control Streamable HTTP GET SSE listening for server-initiated messages. This configuration will be used by `mcp::Client` when connecting over HTTP.

## Inputs

* `.docs/requirements/cpp-mcp-sdk.md` sections: "Streamable HTTP (Required)", "Streamable HTTP GET (Server-Initiated Messages)", and "Streamable HTTP SSE Semantics"
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md` sections: "Listening for Messages from the Server", "Resumability and Redelivery"
* `include/mcp/transport/http.hpp`
* `src/client/client.cpp` (`StreamableHttpClientTransport`)

## Output / Definition of Done

* `include/mcp/transport/http.hpp` will include explicit configuration fields for GET SSE listening in both:
  - `mcp::transport::HttpClientOptions`
  - `mcp::transport::http::StreamableHttpClientOptions`
* `src/client/client.cpp` will propagate `HttpClientOptions` listen settings into `StreamableHttpClientOptions`.
* The default configuration will enable GET listening while remaining compatible with servers that return HTTP 405 for GET.

## Step-by-Step Instructions

1. Add a boolean configuration field to `mcp::transport::HttpClientOptions` that will enable or disable GET SSE listen behavior.
2. Add a corresponding boolean configuration field to `mcp::transport::http::StreamableHttpClientOptions`.
3. Update `mcp::Client::connectHttp(const transport::HttpClientOptions &options)` to set the Streamable HTTP option field.
4. Update `mcp::Client::connectHttp(transport::http::StreamableHttpClientOptions options, ...)` call sites to keep default behavior consistent.
5. Update any relevant comments in `include/mcp/transport/http.hpp` to document how the GET listen option maps to MCP 2025-11-25 transport requirements.

## Verification

* `cmake --build build/vcpkg-unix-release --target mcp_sdk`
