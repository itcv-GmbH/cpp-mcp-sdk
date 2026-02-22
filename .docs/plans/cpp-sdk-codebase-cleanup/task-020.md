# Task ID: task-020
# Task Name: Normalize `include/mcp/client/sampling.hpp` Basename

## Context
This task is responsible for ensuring the sampling create-message context type has a dedicated per-type header whose basename matches the type name in `snake_case` while preserving the existing include path.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/client/sampling.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Output / Definition of Done
*   `include/mcp/client/sampling_create_message_context.hpp` exists and defines the sampling create-message context type.
*   `include/mcp/client/sampling.hpp` contains zero `class` declarations and zero `struct` declarations and includes `include/mcp/client/sampling_create_message_context.hpp`.
*   `tools/checks/check_public_header_one_type.py` reports zero violations for sampling headers.

## Step-by-Step Instructions
1.  Create `include/mcp/client/sampling_create_message_context.hpp` and move the type definition into it without changing declarations.
2.  Update `include/mcp/client/sampling.hpp` to include the new per-type header and to remove the moved `struct` definition.
3.  Build and run unit tests that include `<mcp/client/sampling.hpp>`.
4.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
