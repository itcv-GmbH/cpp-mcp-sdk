# Task ID: task-009
# Task Name: Split `include/mcp/auth/client_registration.hpp`

## Context
This task is responsible for converting `include/mcp/auth/client_registration.hpp` into an umbrella header and introducing per-type headers for all public `class` and `struct` types in the client registration module.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Public Header Organization Rules; Required Header Splits)
*   `include/mcp/auth/client_registration.hpp`
*   `tools/checks/check_public_header_one_type.py`

## Output / Definition of Done
*   `include/mcp/auth/client_registration.hpp` contains zero `class` declarations and zero `struct` declarations.
*   `include/mcp/auth/client_registration.hpp` remains available at its current include path and re-exports the required types by including per-type headers.
*   Per-type headers exist for all top-level `class` and `struct` types formerly defined in `include/mcp/auth/client_registration.hpp`, including at minimum:
    *   `mcp::auth::ClientRegistrationError`
    *   `mcp::auth::ClientCredentialsStore`
    *   `mcp::auth::InMemoryClientCredentialsStore`
*   `tools/checks/check_public_header_one_type.py` reports zero violations for the client registration module headers.

## Step-by-Step Instructions
1.  Create per-type headers under `include/mcp/auth/` for each top-level `class` and `struct` defined in `include/mcp/auth/client_registration.hpp` using `snake_case` basenames.
2.  Move each type declaration into its corresponding per-type header without changing declarations.
3.  Update `include/mcp/auth/client_registration.hpp` to include the per-type headers and to remove all `class` and `struct` declarations.
4.  Build and run unit tests that cover client registration behavior.
5.  Run `tools/checks/check_public_header_one_type.py`.

## Verification
*   `python3 tools/checks/check_public_header_one_type.py`
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release --output-on-failure`
