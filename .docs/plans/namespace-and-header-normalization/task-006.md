# Task ID: task-006
# Task Name: Normalize Security Module Detail Layout

## Context
The security module includes a header that declares `namespace mcp::security::detail` from a non-`detail/` path.

This task will relocate that header into a `detail/` directory and will preserve the namespace mapping rules.

## Inputs
* `include/mcp/security/parsed_origin.hpp`
* `include/mcp/security/*.hpp`
* `src/security/*.cpp`
* SRS: Functional Requirements 5 and 7

## Output / Definition of Done
* `include/mcp/security/detail/parsed_origin.hpp` is required to exist
* `include/mcp/security/parsed_origin.hpp` must not exist
* The relocated header is required to declare symbols in `namespace mcp::security::detail`
* `include/mcp/security/all.hpp` is required to exist and is required to contain only `#include` directives

## Step-by-Step Instructions
1. Create directory `include/mcp/security/detail/`.
2. Relocate `include/mcp/security/parsed_origin.hpp` to `include/mcp/security/detail/parsed_origin.hpp`.
3. Create `include/mcp/security/all.hpp` and ensure it contains only `#include` directives.
4. Update all includes across the repository to use the relocated path.

## Verification
* `python3 tools/checks/run_checks.py`
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release`
