# Task ID: task-012
# Task Name: Normalize Client Module Headers And Namespaces

## Context
Client headers currently declare client-related types in `namespace mcp` and use prohibited umbrella headers.

This task will normalize client namespaces, remove prohibited umbrellas, and ensure the canonical `include/mcp/client/client.hpp` declares `mcp::client::Client`.

## Inputs
* `include/mcp/client/client.hpp`
* `include/mcp/client/client_class.hpp`
* `include/mcp/client/types.hpp`
* `src/client/client.cpp`
* SRS: Functional Requirements 1, 2, 3, and 4

## Output / Definition of Done
* `include/mcp/client/client.hpp` is required to declare `mcp::client::Client` and is required to not be an umbrella header
* `include/mcp/client/client_class.hpp` must not exist
* `include/mcp/client/types.hpp` must not exist
* `include/mcp/client/client_initialize_configuration.hpp` is required to exist and is required to declare `mcp::client::ClientInitializeConfiguration`
* Every type declared under `include/mcp/client/*.hpp` is required to be declared in `namespace mcp::client`
* `include/mcp/client/all.hpp` is required to exist and is required to contain only `#include` directives
* Prohibited umbrella headers under `include/mcp/client/` must not exist outside `all.hpp`

## Step-by-Step Instructions
1. Move the `Client` class declaration from `include/mcp/client/client_class.hpp` into `include/mcp/client/client.hpp` and set its namespace to `mcp::client`.
2. Delete `include/mcp/client/client_class.hpp`.
3. Replace `include/mcp/client/types.hpp` with `include/mcp/client/client_initialize_configuration.hpp` and relocate `ClientInitializeConfiguration` into `namespace mcp::client`.
4. Delete `include/mcp/client/types.hpp`.
5. Delete prohibited client umbrella headers and replace include sites with per-type headers or `<mcp/client/all.hpp>`.
6. Create `include/mcp/client/all.hpp` and ensure it contains only `#include` directives.
7. Update `src/client/client.cpp`, tests, and examples to reference `mcp::client::Client` and other client types.

## Verification
* `python3 tools/checks/run_checks.py`
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
