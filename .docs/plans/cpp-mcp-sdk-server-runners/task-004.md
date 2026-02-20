# Task ID: task-004
# Task Name: Add STDIO Runner Unit Tests

## Context
The runner must enforce the stdio constraints: only protocol messages to stdout and correct error handling on malformed input.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (stdio transport requirements)
* `include/mcp/server/stdio_runner.hpp`
* `include/mcp/server/server.hpp`
* Test patterns in `tests/transport_stdio_test.cpp` (if present) and existing Catch2 conventions

## Output / Definition of Done
* `tests/server_stdio_runner_test.cpp` added.
* `tests/CMakeLists.txt` updated to build and register `mcp_sdk_server_stdio_runner_test`.
* Tests cover:
  * valid initialize flow produces a JSON-RPC response line
  * malformed JSON yields parse error response with `id: null`
  * runner never writes diagnostics to the output stream

## Step-by-Step Instructions
1. Create a minimal `ServerFactory` that returns a configured `mcp::Server` with at least tools capability so initialize succeeds.
2. Use `std::istringstream` as input and `std::ostringstream` for output and stderr.
3. Feed inputs:
   - a valid `initialize` request
   - `notifications/initialized`
   - an invalid JSON line
4. Assert:
   - output contains only newline-delimited JSON objects (no extra text)
   - parse error response uses JSON-RPC parse error code and `id` is absent/null
   - stderr contains diagnostics (optional; controlled by options)

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_server_stdio_runner_test --output-on-failure`
