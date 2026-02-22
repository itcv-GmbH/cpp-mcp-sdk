# Task ID: task-011
# Task Name: Normalize Server Module Headers, Runners, And Namespaces

## Context
Server headers currently declare many server-related types in `namespace mcp` and expose multiple prohibited umbrella headers.

This task will normalize server namespaces, remove prohibited umbrella headers, and ensure the canonical `include/mcp/server/server.hpp` declares `mcp::server::Server`.

## Inputs
* `include/mcp/server/server.hpp`
* `include/mcp/server/server_class.hpp`
* `include/mcp/server/detail/*.hpp`
* `src/server/*.cpp`
* SRS: Functional Requirements 1, 2, 3, 4, and 5

## Output / Definition of Done
* `include/mcp/server/server.hpp` is required to declare `mcp::server::Server` and is required to not be an umbrella header
* `include/mcp/server/server_class.hpp` must not exist
* Every type declared under `include/mcp/server/*.hpp` is required to be declared in `namespace mcp::server`
* Every type declared under `include/mcp/server/detail/*.hpp` is required to be declared in `namespace mcp::server::detail`
* `include/mcp/server/all.hpp` is required to exist and is required to contain only `#include` directives
* Prohibited umbrella headers under `include/mcp/server/` must not exist outside `all.hpp`

## Step-by-Step Instructions
1. Move the `Server` class declaration from `include/mcp/server/server_class.hpp` into `include/mcp/server/server.hpp` and set its namespace to `mcp::server`.
2. Delete `include/mcp/server/server_class.hpp`.
3. Convert runner headers under `include/mcp/server/*.hpp` to per-type headers that declare their public types in `mcp::server`.
4. Update all `include/mcp/server/detail/*.hpp` namespaces to `mcp::server::detail`.
5. Delete prohibited umbrella headers under `include/mcp/server/` and replace include sites with per-type headers or `<mcp/server/all.hpp>`.
6. Create `include/mcp/server/all.hpp` and ensure it contains only `#include` directives.
7. Update `src/server/*.cpp`, tests, and examples to reference `mcp::server::Server`, `mcp::server::StdioServerRunner`, `mcp::server::StreamableHttpServerRunner`, and other server types.

## Verification
* `python3 tools/checks/run_checks.py`
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
