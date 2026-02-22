# Task ID: task-005
# Task Name: Split `include/mcp/transport/http.hpp`

## Context
This task is responsible for converting `include/mcp/transport/http.hpp` into an umbrella header and introducing per-type headers that satisfy the one-type-per-header rule while preserving the existing include path.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules; Required Header Splits)
*   `include/mcp/transport/http.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Output / Definition of Done
*   `include/mcp/transport/http.hpp` contains zero `class` declarations and zero `struct` declarations.
*   `include/mcp/transport/http.hpp` remains available at its current include path and re-exports the required types by including per-type headers.
*   Per-type headers exist for every top-level `class` and `struct` formerly defined in `include/mcp/transport/http.hpp`, including at minimum:
    *   `mcp::transport::HttpServerRuntime`
    *   `mcp::transport::HttpClientRuntime`
    *   `mcp::transport::http::StreamableHttpServer`
    *   `mcp::transport::http::StreamableHttpClient`
    *   `mcp::transport::http::SharedHeaderState`
    *   `mcp::transport::http::SessionHeaderState`
    *   `mcp::transport::http::ProtocolVersionHeaderState`
*   `tools/checks/check_public_header_one_type.py` reports zero violations for `include/mcp/transport/http.hpp` and for all newly added per-type headers.

The per-type header set is required to cover the following top-level `class` and `struct` types currently defined in `include/mcp/transport/http.hpp`:
*   `Header`
*   `ServerTlsConfiguration`
*   `ClientTlsConfiguration`
*   `SessionHeaderState`
*   `ProtocolVersionHeaderState`
*   `SharedHeaderState`
*   `SessionResolution`
*   `RequestValidationOptions`
*   `RequestValidationResult`
*   `HttpEndpointConfig`
*   `HttpServerOptions`
*   `ServerRequest`
*   `SseStreamResponse`
*   `ServerResponse`
*   `StreamableRequestResult`
*   `StreamableHttpServerOptions`
*   `StreamableHttpServer`
*   `StreamableHttpSendResult`
*   `StreamableHttpListenResult`
*   `StreamableHttpClientOptions`
*   `StreamableHttpClient`
*   `HttpClientOptions`
*   `HttpServerRuntime`
*   `HttpClientRuntime`

## Step-by-Step Instructions
1.  Create directory `include/mcp/transport/http/`.
2.  Create per-type headers for each top-level `class` and `struct` currently defined in `include/mcp/transport/http.hpp` using `snake_case` basenames.
3.  Move each type declaration into its corresponding per-type header without changing namespaces, member declarations, defaults, and inline definitions.
4.  Update `include/mcp/transport/http.hpp` to include the per-type headers and to remove all `class` and `struct` declarations.
5.  Update include dependencies within per-type headers so each header compiles standalone.
6.  Build and run unit tests that include `<mcp/transport/http.hpp>`.
7.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
