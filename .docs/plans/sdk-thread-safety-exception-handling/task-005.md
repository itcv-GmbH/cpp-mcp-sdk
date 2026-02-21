# Task ID: task-005
# Task Name: Align JSON-RPC Router Threading And Errors

## Context

The JSON-RPC router is responsible for concurrent in-flight request tracking, timeouts, and response routing. The SDK is required to ensure router thread safety and exception behavior matches the SDK contracts.

## Inputs

* `.docs/requirements/cpp-mcp-sdk.md` section: "The SDK MUST support concurrent in-flight requests"
* `.docs/plans/sdk-thread-safety-exception-handling/thread_safety_contract.md`
* `.docs/plans/sdk-thread-safety-exception-handling/exception_contract.md`
* `include/mcp/jsonrpc/router.hpp`
* `src/jsonrpc/router.cpp`

## Output / Definition of Done

* `include/mcp/jsonrpc/router.hpp` will document the router thread-safety guarantees.
* `src/jsonrpc/router.cpp` will enforce the no-throw rules for background worker contexts.
* `src/jsonrpc/router.cpp` will contain exceptions thrown by user-provided callbacks (request handlers, notification handlers, and progress callbacks) and will report failures via the unified error reporting mechanism.
* A unit test will validate correct response routing with concurrent `sendRequest` calls.
* A unit test will validate that router shutdown completes without throwing.

## Step-by-Step Instructions

1. Audit router public entrypoints for concurrent invocation and update the thread-safety documentation.
2. Ensure all asynchronous callbacks and thread-pool tasks catch exceptions and report them via the unified error reporting mechanism.
3. Add a unit test that issues concurrent requests and validates each future receives the correct response.
4. Add a unit test that destroys a router instance with in-flight requests and asserts shutdown does not throw and promises complete.

## Verification

* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R router -V`
