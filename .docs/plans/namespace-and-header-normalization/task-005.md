# Task ID: task-005
# Task Name: Normalize Transport Module Headers And Namespaces

## Context
The transport include tree currently uses prohibited umbrella headers (`stdio.hpp`, `http.hpp`) and includes several headers under `include/mcp/transport/http/` that declare types in `namespace mcp::transport` instead of `namespace mcp::transport::http`.

This task will normalize transport namespaces and replace prohibited umbrellas with `include/mcp/transport/all.hpp`.

## Inputs
* `include/mcp/transport/transport.hpp`
* `include/mcp/transport/stdio.hpp`
* `include/mcp/transport/http.hpp`
* `include/mcp/transport/http/*.hpp`
* `src/transport/*.cpp`
* SRS: Functional Requirements 1, 4, and 7

## Output / Definition of Done
* `include/mcp/transport/all.hpp` is required to exist and is required to contain only `#include` directives
* `include/mcp/transport/stdio.hpp` must not exist
* `include/mcp/transport/http.hpp` must not exist
* Every type declared in `include/mcp/transport/http/*.hpp` is required to be declared in `namespace mcp::transport::http`
* Every type declared in `include/mcp/transport/*.hpp` is required to be declared in `namespace mcp::transport`

## Step-by-Step Instructions
1. Create `include/mcp/transport/all.hpp` and ensure it contains only `#include` directives for transport public headers.
2. Delete `include/mcp/transport/stdio.hpp` and `include/mcp/transport/http.hpp`.
3. Update all includes across the repository to use `<mcp/transport/all.hpp>` or per-type headers.
4. Update namespaces for all transport HTTP headers so that declared types conform to `mcp::transport::http`.
5. Update `src/transport/*.cpp` and relevant headers to match the namespace changes.

## Verification
* `python3 tools/checks/run_checks.py`
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
