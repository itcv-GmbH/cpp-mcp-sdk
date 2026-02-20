# Task ID: task-016
# Task Name: Update Reference Interop Fixture to Use HTTP Runner

## Context
The repository contains reference interoperability integration tests under `tests/integration/` that exercise the C++ SDK server against the official Python reference SDK. This feature adds Streamable HTTP server runners and server-issued `MCP-Session-Id` issuance on initialize. The integration fixture must use the new runner APIs to validate runner behavior end-to-end and must stop manually overriding session headers.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP transport; session management)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md` (Streamable HTTP; sessions)
* `tests/integration/cpp_server_fixture.cpp`
* `tests/integration/scripts/reference_client_to_cpp_server.py`
* `tests/integration/CMakeLists.txt`
* `include/mcp/server/streamable_http_runner.hpp` (from `task-005`)
* `src/transport/http_server.cpp` behavior from `task-014`

## Output / Definition of Done
* `tests/integration/cpp_server_fixture.cpp` is modified to:
  * construct and start an `mcp::StreamableHttpServerRunner` using a `ServerFactory`
  * set `mcp::transport::http::StreamableHttpServerOptions.http.requireSessionId = true`
  * remove manual `MCP-Session-Id` header injection and remove manual `StreamableHttpServer::upsertSession(...)` calls
  * preserve server authorization configuration and the outbound sampling/elicitation assertions
* `tests/integration/scripts/reference_client_to_cpp_server.py` is modified to assert:
  * an authenticated initialize probe returns HTTP 200 and includes `MCP-Session-Id` header
  * two authenticated initialize probes return different `MCP-Session-Id` values
  * a non-initialize POST without `MCP-Session-Id` returns HTTP 400 when `requireSessionId=true`
* `tests/integration/README.md` is updated to state that the C++ server fixture uses the Streamable HTTP runner.

## Step-by-Step Instructions
1. Update `tests/integration/cpp_server_fixture.cpp` to define `auto makeServer() -> std::shared_ptr<mcp::Server>` that:
   - registers the tool/resource/prompt fixtures
   - installs outbound message sender wiring required for server-initiated requests
   - starts the outbound sampling/elicitation worker after receiving `notifications/initialized`
2. Replace direct `mcp::transport::http::StreamableHttpServer` and `mcp::transport::HttpServerRuntime` wiring with `mcp::StreamableHttpServerRunner`.
3. Ensure runner options set:
   - `options.http.authorization` matches the existing authorization configuration
   - `options.http.endpoint.bindAddress`, `options.http.endpoint.port`, and `options.http.endpoint.path` use the existing CLI options
   - `options.http.requireSessionId = true`
4. Ensure the fixture uses runner accessors to print the resolved listening port and blocks on `stdin` until EOF.
5. Update `tests/integration/scripts/reference_client_to_cpp_server.py` to add explicit HTTP probes for:
   - `MCP-Session-Id` issuance on authenticated initialize
   - session uniqueness across separate initialize requests
   - HTTP 400 on non-initialize requests missing `MCP-Session-Id`
6. Ensure the existing end-to-end authenticated MCP session flow continues to validate tools/resources/prompts and outbound sampling/elicitation.
7. Update `tests/integration/README.md` coverage text to reference runner-based server fixture behavior.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON && cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R integration_reference_client_to_cpp_server --output-on-failure`
* `ctest --test-dir build/vcpkg-unix-release -R integration_reference --output-on-failure`
