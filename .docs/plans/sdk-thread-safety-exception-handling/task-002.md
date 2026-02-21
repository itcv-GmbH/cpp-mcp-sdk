# Task ID: task-002
# Task Name: Define Exception Contract

## Context

The SDK currently mixes exception suppression, exception-to-JSON-RPC conversion, and direct exception propagation. The SDK is required to define a consistent exception contract that covers public API behavior, background thread boundaries, and destructor behavior.

## Inputs

* `include/mcp/client/client.hpp`
* `include/mcp/jsonrpc/router.hpp`
* `include/mcp/lifecycle/session.hpp`
* `include/mcp/transport/http.hpp`
* `include/mcp/transport/stdio.hpp`
* `src/client/client.cpp`
* `src/jsonrpc/router.cpp`
* `src/server/stdio_runner.cpp`

## Output / Definition of Done

* `.docs/plans/sdk-thread-safety-exception-handling/exception_contract.md` will exist.
* The contract document will define:
  - which public entrypoints will throw exceptions
  - which public entrypoints are required to be `noexcept`
  - how protocol-level failures are represented (JSON-RPC error responses versus C++ exceptions)
  - a no-throw rule for all thread entrypoints created by the SDK
  - a no-throw rule for destructors
* Public headers listed in Inputs will contain an explicit "Exceptions" section that matches the contract.

## Step-by-Step Instructions

1. Create `.docs/plans/sdk-thread-safety-exception-handling/exception_contract.md`.
2. Define exception classes and categories that the SDK will use for:
   - invalid arguments
   - invalid lifecycle state
   - transport failures
   - protocol violations detected at runtime
3. Define the required mapping rules between MCP protocol errors and C++ exceptions.
4. Update each listed public header to include an "Exceptions" section that matches the contract.

## Verification

* `cmake --build build/vcpkg-unix-release`
