# Task ID: task-004
# Task Name: Enforce No-Throw Thread Boundaries

## Context

The SDK starts multiple threads. Any exception escaping a thread entrypoint will call `std::terminate`. The SDK is required to enforce a consistent no-throw boundary for every thread entrypoint created by the SDK.

## Inputs

* `.docs/plans/sdk-thread-safety-exception-handling/exception_contract.md`
* `.docs/plans/sdk-thread-safety-exception-handling/task-003.md`
* `src/client/client.cpp`
* `include/mcp/detail/inbound_loop.hpp`
* `src/detail/inbound_loop.cpp`
* `src/transport/http_runtime.cpp`
* `src/server/streamable_http_runner.cpp`
* `src/server/stdio_runner.cpp`
* `src/jsonrpc/router.cpp`
* `src/transport/stdio.cpp`
* `src/transport/streamable_http_client_transport.cpp`

## Output / Definition of Done

* Every `std::thread` entrypoint created by the SDK will be wrapped in a `noexcept` lambda that catches all exceptions.
* Every caught exception will be reported via the unified error reporting mechanism.
* The thread boundary wrapper will be used consistently across the SDK.
* Work items posted to `boost::asio::thread_pool` will contain exceptions and will report failures via the unified error reporting mechanism.

## Step-by-Step Instructions

1. Add a single internal helper in `src/detail/` or `include/mcp/detail/` that wraps a callable in a catch-all boundary and reports failures.
2. Replace direct thread entrypoints with the wrapper in:
    - stdio client reader thread
    - HTTP server runtime thread
    - server runner async entrypoints
    - any detached thread usage in JSON-RPC router cleanup logic
3. Add a test-only hook that triggers a controlled exception inside a background loop and asserts the error callback records the failure.

## Verification

* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R threading -V`
