# Task ID: task-009
# Task Name: Normalize SDK Module Namespaces And Remove Deprecated Top-Level Wrappers

## Context
The `include/mcp/sdk/` headers currently declare types in `namespace mcp`, which violates the namespace layout policy.

The repository also contains deprecated top-level wrapper headers under `include/mcp/*.hpp` that are umbrella-style re-exports, which violates the umbrella header policy.

## Inputs
* `include/mcp/sdk/version.hpp`
* `include/mcp/sdk/errors.hpp`
* `include/mcp/sdk/error_reporter.hpp`
* `include/mcp/version.hpp`
* `include/mcp/errors.hpp`
* `include/mcp/error_reporter.hpp`
* `src/version.cpp`
* SRS: Functional Requirements 1, 4, and 8

## Output / Definition of Done
* Types declared under `include/mcp/sdk/*.hpp` are required to be declared in `namespace mcp::sdk`
* `include/mcp/sdk/all.hpp` is required to exist and is required to contain only `#include` directives
* Deprecated top-level wrapper headers that contain only `#include` directives must not exist
* All include sites are required to include canonical SDK headers

## Step-by-Step Instructions
1. Update namespaces in `include/mcp/sdk/version.hpp`, `include/mcp/sdk/errors.hpp`, and `include/mcp/sdk/error_reporter.hpp` so their public symbols are declared in `namespace mcp::sdk`.
2. Create `include/mcp/sdk/all.hpp` and ensure it contains only `#include` directives.
3. Delete deprecated wrapper headers under `include/mcp/` that contain only re-exports.
4. Update code, tests, examples, and docs to include `<mcp/sdk/version.hpp>`, `<mcp/sdk/errors.hpp>`, and `<mcp/sdk/error_reporter.hpp>` via canonical paths.

## Verification
* `python3 tools/checks/run_checks.py`
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
