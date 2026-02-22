# Task ID: task-014
# Task Name: Update Documentation, Examples, And Tests To Canonical Includes

## Context
This task will update documentation and code samples to match the canonical namespaces and canonical include paths introduced by the refactor.

The SRS requires `docs/api_overview.md` to be updated.

## Inputs
* `docs/api_overview.md`
* `docs/quickstart_client.md`
* `docs/quickstart_server.md`
* `docs/version_policy.md`
* `examples/**/*.cpp`
* `tests/**/*.cpp`
* SRS: Functional Requirements 8

## Output / Definition of Done
* `docs/api_overview.md` is required to reference canonical namespaces and header paths
* Code snippets in docs are required to include `include/mcp/client.hpp`, `include/mcp/server.hpp`, and `include/mcp/session.hpp` when they use `mcp::Client`, `mcp::Server`, or `mcp::Session`
* Examples and tests are required to compile against the canonical header layout

## Step-by-Step Instructions
1. Update `docs/api_overview.md` to use canonical namespaces and header paths defined by the SRS.
2. Update remaining docs that contain `#include <mcp/...>` snippets to use the canonical include paths.
3. Update `examples/**/*.cpp` includes and namespaces to match the refactor.
4. Update `tests/**/*.cpp` includes and namespaces to match the refactor.

## Verification
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
