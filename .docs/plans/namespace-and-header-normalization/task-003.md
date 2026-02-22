# Task ID: task-003
# Task Name: Normalize Schema Module Headers And Namespaces

## Context
This task will normalize schema validation headers and enforce canonical per-type header requirements.

## Inputs
* `include/mcp/schema/validator.hpp`
* `include/mcp/schema/validator_class.hpp`
* `include/mcp/schema/pinned_schema.hpp`
* `src/schema/validator.cpp`
* SRS: Functional Requirements 1, 3, 4, and 5

## Output / Definition of Done
* `include/mcp/schema/validator.hpp` is required to declare `mcp::schema::Validator` and is required to not be an umbrella header
* `include/mcp/schema/validator_class.hpp` must not exist
* `include/mcp/schema/all.hpp` is required to exist and is required to contain only `#include` directives
* Any symbol currently declared in `namespace mcp::schema::detail` from a non-`detail/` path is required to be relocated to `include/mcp/schema/detail/`
* `include/mcp/schema/pinned_schema.hpp` is required to be relocated to `include/mcp/schema/detail/pinned_schema.hpp`

## Step-by-Step Instructions
1. Move the `mcp::schema::Validator` declaration from `include/mcp/schema/validator_class.hpp` into `include/mcp/schema/validator.hpp`.
2. Delete `include/mcp/schema/validator_class.hpp`.
3. Create `include/mcp/schema/all.hpp` and ensure it contains only `#include` directives for schema public headers.
4. Relocate `include/mcp/schema/pinned_schema.hpp` to `include/mcp/schema/detail/pinned_schema.hpp` and keep its namespace as `mcp::schema::detail`.
5. Update all includes across the repository to use the new canonical paths.

## Verification
* `python3 tools/checks/run_checks.py`
* `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
