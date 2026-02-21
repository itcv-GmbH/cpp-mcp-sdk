# Task ID: task-003
# Task Name: Add Unified Error Reporting Mechanism

## Context

Multiple SDK components catch and suppress exceptions without surfacing failures to callers. This behavior makes production issues hard to diagnose and makes background thread failures silent. The SDK is required to implement a unified error reporting mechanism and to wire it into all background execution contexts.

## Inputs

* `.docs/plans/sdk-thread-safety-exception-handling/thread_safety_contract.md`
* `.docs/plans/sdk-thread-safety-exception-handling/exception_contract.md`
* `src/client/client.cpp` (stdio subprocess reader thread, async pools)
* `src/detail/inbound_loop.cpp` (transport inbound loop containment)
* `src/transport/http_runtime.cpp` (server thread)
* `src/jsonrpc/router.cpp` (thread pool completion and timeout logic)
* `src/server/stdio_runner.cpp` (exception conversion patterns)
* `src/transport/streamable_http_client_transport.cpp` (GET listen loop)

## Output / Definition of Done

* A new public or internal error reporting interface will exist in a dedicated header.
* Client and server configuration types will accept an error reporting callback.
* All background loops will report exceptions through this callback.
* Every callback invocation site will contain exceptions and will never terminate the process.

## Step-by-Step Instructions

1. Create a dedicated error reporting header under `include/mcp/` that defines:
    - an error event type containing a component identifier and a message
    - an error callback type that is treated as potentially throwing and is invoked only from catch-all boundaries
2. Update configuration types for:
   - `mcp::Client`
   - server runners
   - HTTP runtime
   to accept an error callback.
3. Update each background execution context to catch all exceptions and to report them to the error callback.
4. Ensure the SDK will continue operating where recovery is defined by the thread-safety and exception contracts.

## Verification

* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -R client -V`
