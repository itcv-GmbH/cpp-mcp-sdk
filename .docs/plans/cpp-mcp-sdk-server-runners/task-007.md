# Task ID: task-007
# Task Name: Add Dual-Transport Example (STDIO + HTTP)

## Context
This example is required to demonstrate that the same server logic is deployable both locally (stdio) and remotely (HTTP). The example will run both transports in one process while preventing lifecycle state sharing across transports.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (stdio + Streamable HTTP requirements)
* `include/mcp/server/runners.hpp`
* `include/mcp/server/combined_runner.hpp`
* Existing server registration patterns in `examples/stdio_server/main.cpp`
* Existing HTTP wiring in `examples/http_server_auth/main.cpp`

## Output / Definition of Done
* New example added at `examples/dual_transport_server/main.cpp` demonstrating:
  * a shared `ServerFactory` function building an `mcp::Server` with tools/resources/prompts
  * start both transports via `CombinedServerRunner`, and start individual runners explicitly as a separate section
  * run STDIO in foreground while HTTP runs in background
  * graceful shutdown on stdin EOF / SIGINT (best-effort)
* Example builds under `MCP_SDK_BUILD_EXAMPLES=ON`.

## Step-by-Step Instructions
1. Extract server setup into `auto makeServer() -> std::shared_ptr<mcp::Server>`.
2. Use `CombinedServerRunner`:
    - configure Streamable HTTP bind/port/path
    - enable `requireSessionId=true` for spec-conformant multi-client isolation
    - start HTTP transport
    - run STDIO transport (blocking)
3. On exit, stop the combined runner (stops HTTP; STDIO exits on EOF).
4. The example must include the equivalent explicit composition using `StdioServerRunner` + `StreamableHttpServerRunner` for fine-grained orchestration.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON && cmake --build build/vcpkg-unix-release --target mcp_sdk_example_dual_transport_server`
