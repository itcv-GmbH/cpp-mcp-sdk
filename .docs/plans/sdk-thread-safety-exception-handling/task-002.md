# Task ID: task-002
# Task Name: Define Exception Contract

## Context

The SDK currently mixes exception suppression, exception-to-JSON-RPC conversion, and direct exception propagation. The SDK is required to define a consistent exception contract that covers public API behavior, background thread boundaries, and destructor behavior.

## Inputs

* `.docs/plans/sdk-thread-safety-exception-handling/thread_safety_contract.md`
* `include/mcp/client/client.hpp`
* `include/mcp/jsonrpc/router.hpp`
* `include/mcp/jsonrpc/messages.hpp`
* `include/mcp/lifecycle/session.hpp`
* `include/mcp/errors.hpp`
* `include/mcp/detail/inbound_loop.hpp`
* `include/mcp/transport/http.hpp`
* `include/mcp/transport/stdio.hpp`
* `src/client/client.cpp`
* `src/jsonrpc/router.cpp`
* `src/server/stdio_runner.cpp`

## Output / Definition of Done

* `.docs/plans/sdk-thread-safety-exception-handling/exception_contract.md` will be updated.
* The contract document will define:
  - which public entrypoints throw exceptions and which are required to be non-throwing
  - which public entrypoints are required to be `noexcept`
  - how protocol-level failures are represented (JSON-RPC error responses versus C++ exceptions)
  - exception containment rules for user-provided callbacks
  - a no-throw rule for all background execution contexts created by the SDK
  - a no-throw rule for destructors
* Public headers listed in Inputs will contain an explicit "Exceptions" section that matches the contract.

## Step-by-Step Instructions

1. Update `.docs/plans/sdk-thread-safety-exception-handling/exception_contract.md`.
2. Define an exception taxonomy and mapping rules that the SDK will use for:
   - invalid arguments
   - invalid lifecycle state
   - transport failures
   - protocol violations detected at runtime
3. Define the required mapping rules between MCP protocol errors and C++ exceptions.
4. Update each listed public header to include an "Exceptions" section that matches the contract.

## Verification

* `cmake --build build/vcpkg-unix-release`
