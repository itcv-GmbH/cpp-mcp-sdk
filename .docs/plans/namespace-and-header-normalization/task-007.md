# Task ID: task-007
# Task Name: Normalize Auth Module Umbrella Headers

## Context
The auth module contains umbrella headers at non-`all.hpp` paths.

This task will remove prohibited umbrella headers and will provide a single module umbrella at `include/mcp/auth/all.hpp`.

## Inputs
* `include/mcp/auth/provider.hpp`
* `include/mcp/auth/oauth_server.hpp`
* `include/mcp/auth/client_registration.hpp`
* `include/mcp/auth/loopback_receiver.hpp`
* `include/mcp/auth/protected_resource_metadata.hpp`
* SRS: Functional Requirements 4 and 7

## Output / Definition of Done
* `include/mcp/auth/all.hpp` is required to exist and is required to contain only `#include` directives
* Umbrella headers outside `include/mcp/auth/all.hpp` must not exist
* All includes of removed umbrella headers are required to be replaced with per-type headers or with `<mcp/auth/all.hpp>`

## Step-by-Step Instructions
1. Create `include/mcp/auth/all.hpp` and ensure it contains only `#include` directives.
2. Delete auth umbrella headers that exist outside `all.hpp`.
3. Update all include sites across `include/`, `src/`, `tests/`, `examples/`, and `docs/` to use canonical headers.

## Verification
* `python3 tools/checks/run_checks.py`
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release`
