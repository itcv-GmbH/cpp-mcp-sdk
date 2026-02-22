# Task ID: task-004
# Task Name: Relocate Thread Boundary Header and Fix Includes

## Context
This task is responsible for moving the internal threading boundary header into the public detail include tree and removing directory-traversing relative includes.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Include Policy)
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Thread boundary relocation requirement)
*   `src/detail/thread_boundary.hpp`
*   `src/jsonrpc/router.cpp`
*   `src/server/server.cpp`
*   `src/server/stdio_runner.cpp`
*   `tests/detail/thread_boundary_test.cpp`

## Output / Definition of Done
*   `src/detail/thread_boundary.hpp` is relocated to `include/mcp/detail/thread_boundary.hpp`.
*   All call sites include the header via `<mcp/detail/thread_boundary.hpp>`.
*   `tools/checks/check_include_policy.py` reports zero violations for `src/` and `tests/`.

## Step-by-Step Instructions
1.  Move `src/detail/thread_boundary.hpp` to `include/mcp/detail/thread_boundary.hpp`.
2.  Update `src/jsonrpc/router.cpp` to replace `#include "../detail/thread_boundary.hpp"` with `#include <mcp/detail/thread_boundary.hpp>`.
3.  Update `src/server/server.cpp` to replace `#include "../detail/thread_boundary.hpp"` with `#include <mcp/detail/thread_boundary.hpp>`.
4.  Update `src/server/stdio_runner.cpp` to replace `#include "../detail/thread_boundary.hpp"` with `#include <mcp/detail/thread_boundary.hpp>`.
5.  Update `tests/detail/thread_boundary_test.cpp` to remove the `../../src/detail/thread_boundary.hpp` include and include `<mcp/detail/thread_boundary.hpp>`.
6.  Run the include policy enforcement script.
7.  Build and run the thread boundary unit tests.

## Verification
*   `python3 tools/checks/check_include_policy.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R thread_boundary --output-on-failure`
