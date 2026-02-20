# Task ID: task-007
# Task Name: Add Dual-Transport Example (STDIO + HTTP)

## Context
Users commonly want the same server logic to be deployable both locally (stdio) and remotely (HTTP). With per-session `ServerFactory` runners, users can run both transports in one process without sharing lifecycle state.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (stdio + Streamable HTTP requirements)
* `include/mcp/server/runners.hpp`
* `include/mcp/server/combined_runner.hpp`
* Existing server registration patterns in `examples/stdio_server/main.cpp`
* Existing HTTP wiring in `examples/http_server_auth/main.cpp`

## Output / Definition of Done
* New example added at `examples/dual_transport_server/main.cpp` demonstrating:
  * a shared `ServerFactory` function building an `mcp::Server` with tools/resources/prompts
  * start both transports via `CombinedServerRunner` (preferred), or start individual runners explicitly
  * run STDIO in foreground while HTTP runs in background
  * graceful shutdown on stdin EOF / SIGINT (best-effort)
* Example builds under `MCP_SDK_BUILD_EXAMPLES=ON`.

## Step-by-Step Instructions
1. Extract server setup into `auto makeServer() -> std::shared_ptr<mcp::Server>`.
2. Prefer `CombinedServerRunner`:
   - configure Streamable HTTP bind/port/path
   - start HTTP transport
   - run STDIO transport (blocking)
3. On exit, stop the combined runner (stops HTTP; STDIO exits on EOF).
4. (Optional) In the example, show the equivalent explicit composition using `StdioServerRunner` + `StreamableHttpServerRunner` for users who want fine-grained control.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_BUILD_EXAMPLES=ON && cmake --build build/vcpkg-unix-release --target mcp_sdk_example_dual_transport_server`
