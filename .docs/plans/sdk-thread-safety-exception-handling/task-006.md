# Task ID: task-006
# Task Name: Align Transports And Runners Threading And Errors

## Context

Transports and runners create background loops and interact with I/O resources. The SDK is required to enforce consistent stop semantics, no-throw destructor behavior, and error reporting behavior across all transports and runners.

## Inputs

* `.docs/plans/sdk-thread-safety-exception-handling/thread_safety_contract.md`
* `.docs/plans/sdk-thread-safety-exception-handling/exception_contract.md`
* `include/mcp/transport/transport.hpp`
* `include/mcp/transport/stdio.hpp`
* `include/mcp/transport/http.hpp`
* `include/mcp/server/stdio_runner.hpp`
* `include/mcp/server/streamable_http_runner.hpp`
* `src/client/client.cpp` (stdio client transport)
* `src/transport/stdio.cpp`
* `src/transport/http_client.cpp`
* `src/transport/http_runtime.cpp`
* `src/server/stdio_runner.cpp`
* `src/server/streamable_http_runner.cpp`

## Output / Definition of Done

* Every transport and runner will implement:
  - idempotent `start()`
  - idempotent `stop()`
  - no-throw destructors
  - no-throw background thread entrypoints
* All background loop failures will be reported via the unified error reporting mechanism.

## Step-by-Step Instructions

1. Audit `start()` and `stop()` for each transport and runner and update behavior to match the thread-safety contract.
2. Ensure `stop()` joins threads deterministically and does not deadlock.
3. Replace exception suppression with error reporting through the unified mechanism.
4. Add tests that:
   - start and stop transports repeatedly
   - destroy transports while background work is active
   - validate no exceptions escape destructors

## Verification

* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R transport -V`
