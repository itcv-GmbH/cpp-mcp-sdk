# Task ID: task-026
# Task Name: Split `include/mcp/schema/validator.hpp`

## Context
This task is responsible for converting `include/mcp/schema/validator.hpp` into an umbrella header and introducing per-type headers for validator public types.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules)
*   `include/mcp/schema/validator.hpp`
*   `src/schema/validator.cpp`
*   `tools/checks/check_public_header_one_type.py`

## Output / Definition of Done
*   `include/mcp/schema/validator.hpp` contains zero `class` declarations and zero `struct` declarations.
*   Per-type headers exist under `include/mcp/schema/` for the top-level `class` and `struct` types formerly defined in `include/mcp/schema/validator.hpp`:
    *   `ValidationDiagnostic`
    *   `ValidationResult`
    *   `PinnedSchemaMetadata`
    *   `Validator`
*   `tools/checks/check_public_header_one_type.py` reports zero violations for schema validator headers.

## Step-by-Step Instructions
1.  Create one per-type header under `include/mcp/schema/` for each listed type using `snake_case` basenames.
2.  Move each type declaration into its corresponding per-type header without changing declarations.
3.  Update `include/mcp/schema/validator.hpp` to include the per-type headers and to remove all `class` and `struct` declarations.
4.  Update `src/schema/validator.cpp` includes to remain valid after the split.
5.  Build and run unit tests that cover validator behavior.
6.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
