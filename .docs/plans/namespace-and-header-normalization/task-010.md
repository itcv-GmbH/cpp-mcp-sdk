# Task ID: task-010
# Task Name: Normalize Lifecycle Session Headers And Namespaces

## Context
The lifecycle session headers are currently an umbrella at `include/mcp/lifecycle/session.hpp` and declare session-related types in `namespace mcp`.

This task will:
- make `include/mcp/lifecycle/session.hpp` a canonical per-type header for `mcp::lifecycle::Session`
- move `session_class.hpp` content into the canonical header
- move all headers under `include/mcp/lifecycle/session/` into `namespace mcp::lifecycle::session`
- create `include/mcp/lifecycle/all.hpp` as the only lifecycle umbrella

## Inputs
* `include/mcp/lifecycle/session.hpp`
* `include/mcp/lifecycle/session/session_class.hpp`
* `include/mcp/lifecycle/session/*.hpp`
* `src/lifecycle/session.cpp`
* SRS: Functional Requirements 1, 2, 3, and 4

## Output / Definition of Done
* `include/mcp/lifecycle/session.hpp` is required to declare `mcp::lifecycle::Session`
* `include/mcp/lifecycle/session/session_class.hpp` must not exist
* Every type declared in `include/mcp/lifecycle/session/*.hpp` is required to be declared in `namespace mcp::lifecycle::session`
* `include/mcp/lifecycle/all.hpp` is required to exist and is required to contain only `#include` directives
* `mcp::Session` is required to be implemented as an alias only in `include/mcp/session.hpp` and must not exist as a concrete type

## Step-by-Step Instructions
1. Move the `Session` class declaration from `include/mcp/lifecycle/session/session_class.hpp` into `include/mcp/lifecycle/session.hpp` and set its namespace to `mcp::lifecycle`.
2. Delete `include/mcp/lifecycle/session/session_class.hpp`.
3. Update namespaces for all headers under `include/mcp/lifecycle/session/` so their declared types live in `mcp::lifecycle::session`.
4. Create `include/mcp/lifecycle/all.hpp` and ensure it contains only `#include` directives.
5. Update all references across the repository from `mcp::SessionOptions` and similar session component types to `mcp::lifecycle::session::SessionOptions` and the corresponding canonical namespaces.
6. Update `src/lifecycle/session.cpp` to match the new namespaces.

## Verification
* `python3 tools/checks/run_checks.py`
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
