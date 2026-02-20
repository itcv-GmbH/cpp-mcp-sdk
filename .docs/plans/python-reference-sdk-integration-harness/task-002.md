# Task ID: task-002
# Task Name: Create Python Raw JSON-RPC Harness

## Context
The existing integration scripts rely on high-level Python reference SDK APIs, which do not provide complete visibility into notifications and some utilities. This task will create a reusable Python harness that sends and receives raw MCP JSON-RPC messages over Streamable HTTP and STDIO and exposes deterministic notification capture.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (JSON-RPC conformance and protocol surface requirements)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/ping.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/cancellation.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/progress.md`
* `tests/integration/scripts/reference_client_to_cpp_server.py` (Existing Streamable HTTP usage)
* `tests/integration/scripts/reference_client_to_cpp_stdio_server.py` (Existing STDIO usage)

## Output / Definition of Done
* `tests/integration/python/harness.py` exists and provides process lifecycle helpers and timeouts.
* `tests/integration/python/streamable_http_raw.py` exists and implements:
  - POST request sending with required headers
  - GET SSE listening and message decoding
  - JSON-RPC request/response correlation and notification fan-out
* `tests/integration/python/stdio_raw.py` exists and implements:
  - subprocess spawn
  - stdin/stdout framing for MCP STDIO
  - JSON-RPC request/response correlation and notification fan-out
* `tests/integration/python/assertions.py` exists and provides reusable assertions for:
  - JSON-RPC response shape
  - MCP error codes and messages
  - notification name matching

## Step-by-Step Instructions
1. Create the package directory `tests/integration/python/` and add `__init__.py`.
2. Implement `tests/integration/python/harness.py` with:
   - `find_free_port()` for localhost HTTP fixtures
   - `start_process()` and `stop_process()` that enforce timeouts and kill escalation
   - `wait_for_readiness()` that reads fixture stdout markers
3. Implement Streamable HTTP raw transport in `tests/integration/python/streamable_http_raw.py`:
   - The client must set `Content-Type: application/json` and `Accept: application/json, text/event-stream`.
   - The client must set `MCP-Protocol-Version` and handle `MCP-Session-Id` semantics.
   - The client must decode SSE frames into JSON-RPC messages and deliver notifications to a queue.
4. Implement STDIO raw transport in `tests/integration/python/stdio_raw.py`:
   - The client must write newline-delimited JSON per the MCP STDIO transport.
   - The client must parse stdout lines into JSON-RPC messages and deliver notifications to a queue.
5. Implement shared assertions in `tests/integration/python/assertions.py`.
6. Update existing integration scripts to import and use the shared harness where it reduces duplication.

## Verification
* `cmake --preset vcpkg-unix-release -DMCP_SDK_INTEGRATION_TESTS=ON`
* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_integration_reference_setup_python_sdk --output-on-failure`
