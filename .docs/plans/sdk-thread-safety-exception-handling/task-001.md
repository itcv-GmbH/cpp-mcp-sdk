# Task ID: task-001
# Task Name: Define Thread-Safety Contract

## Context

The SDK contains multiple concurrency mechanisms (std::thread, atomics, mutexes, Boost.Asio thread pools). The SDK is required to define an explicit, consistent thread-safety contract so that integrators and contributors will know which operations are safe concurrently and which operations require external serialization.

## Inputs

* `.docs/requirements/cpp-mcp-sdk.md` section: "The SDK MUST support concurrent in-flight requests"
* `include/mcp/client/client.hpp`
* `include/mcp/jsonrpc/router.hpp`
* `include/mcp/lifecycle/session.hpp`
* `include/mcp/transport/transport.hpp`
* `include/mcp/transport/http.hpp`
* `include/mcp/transport/stdio.hpp`

## Output / Definition of Done

* `.docs/plans/sdk-thread-safety-exception-handling/thread_safety_contract.md` will exist.
* The contract document will define:
  - thread-safety classification for each public type
  - allowed concurrent entrypoints
  - lock ordering rules for internal mutexes
  - lifecycle rules for `start()` and `stop()`
* Public headers listed in Inputs will contain an explicit "Thread Safety" section that matches the contract.

## Step-by-Step Instructions

1. Create `.docs/plans/sdk-thread-safety-exception-handling/thread_safety_contract.md`.
2. Enumerate the public types that participate in concurrency:
   - `mcp::Client`
   - `mcp::Session`
   - `mcp::jsonrpc::Router`
   - `mcp::transport::Transport` and concrete transports
   - server runners and HTTP runtime types
3. For each type, specify the thread-safety classification and list entrypoints that are required to be safe under concurrent invocation.
4. Define a single lock ordering rule set for shared locks across the SDK.
5. Update each listed public header to include a "Thread Safety" section that matches the contract document.

## Verification

* `cmake --build build/vcpkg-unix-release`
