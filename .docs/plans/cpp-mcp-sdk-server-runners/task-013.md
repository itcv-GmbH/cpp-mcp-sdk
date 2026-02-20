# Task ID: task-013
# Task Name: Add Combined Runner Unit Tests

## Context
The combined runner is orchestration glue; tests should prove that starting one or both transports does not regress correctness or shutdown behavior, and that it uses a `ServerFactory` without violating session isolation expectations.

## Inputs
* `include/mcp/server/combined_runner.hpp`
* `tests/CMakeLists.txt`
* Catch2 test conventions used across `tests/`

## Output / Definition of Done
* `tests/server_combined_runner_test.cpp` added.
* `tests/CMakeLists.txt` updated to build and register `mcp_sdk_server_combined_runner_test`.
* Tests cover:
  * Combined runner can run with stdio-only enabled (in-memory streams) and produces valid JSON-RPC output
  * Combined runner can start HTTP-only enabled and reports `isRunning()` / `localPort()` (or equivalent)
  * Combined runner can start HTTP then run stdio (in async mode using in-memory streams) without hangs

## Step-by-Step Instructions
1. Use a `ServerFactory` that produces a minimal server that can initialize.
2. For stdio-only mode:
   - provide `std::istringstream` input containing an initialize request and EOF
   - assert output contains a valid JSON-RPC response line
3. For HTTP-only mode:
   - start HTTP runner on ephemeral port and assert it reports running
   - stop and assert it stops cleanly
4. For both-enabled mode:
   - start HTTP
   - run stdio in a joinable thread (or runner async API)
   - stop HTTP and join stdio thread after EOF input completes
5. Keep tests deterministic (no long sleeps); use bounded timeouts if waiting on threads.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_server_combined_runner_test --output-on-failure`
