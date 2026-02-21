# Task ID: task-007
# Task Name: Align Client And Session Threading And Errors

## Context

`mcp::Client` and `mcp::Session` coordinate transport lifecycle, JSON-RPC routing, and handler execution. The SDK is required to enforce consistent thread-safety behavior and exception behavior for these types.

## Inputs

* `.docs/plans/sdk-thread-safety-exception-handling/thread_safety_contract.md`
* `.docs/plans/sdk-thread-safety-exception-handling/exception_contract.md`
* `include/mcp/client/client.hpp`
* `include/mcp/lifecycle/session.hpp`
* `src/client/client.cpp`
* `src/lifecycle/session.cpp`

## Output / Definition of Done

* Public headers will document:
  - which `Client` entrypoints are safe concurrently
  - which callbacks are invoked on which threads
  - which entrypoints will throw
* Provider callback exception handling will be consistent and will not leak exception objects across thread boundaries.
* Client and session background execution failures will be reported via the unified error reporting mechanism.
* Client stop and destruction behavior will be deterministic and will not deadlock.

## Step-by-Step Instructions

1. Audit `Client` entrypoints and document required concurrency guarantees.
2. Audit `Session` handler threading configuration and document required callback threading behavior.
3. Ensure handler exceptions are converted to JSON-RPC error responses where the protocol defines a response.
4. Ensure background loop exceptions are reported via the unified error reporting mechanism.
5. Add tests that validate:
    - server-initiated request handling does not deadlock while the client is stopping
    - provider exceptions result in deterministic error responses

## Verification

* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R client -V`
