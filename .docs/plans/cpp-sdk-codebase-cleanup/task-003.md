# Task ID: task-003
# Task Name: Relocate Auth Implementation Sources

## Context
This task is responsible for enforcing module-aligned source placement by relocating auth implementation sources from `src/` root into `src/auth/`.

## Inputs
*   `.docs/requirements/cpp-sdk-codebase-cleanup.md` (Directory Layout and File Placement; Build System Updates)
*   `CMakeLists.txt` (`MCP_SDK_SOURCES`)
*   Source files listed in the SRS relocation set

## Output / Definition of Done
*   The following files are relocated:
    *   `src/auth_client_registration.cpp` is moved to `src/auth/client_registration.cpp`
    *   `src/auth_loopback_receiver.cpp` is moved to `src/auth/loopback_receiver.cpp`
    *   `src/auth_oauth_client.cpp` is moved to `src/auth/oauth_client.cpp`
    *   `src/auth_oauth_client_disabled.cpp` is moved to `src/auth/oauth_client_disabled.cpp`
    *   `src/auth_protected_resource_metadata.cpp` is moved to `src/auth/protected_resource_metadata.cpp`
*   `CMakeLists.txt` updates `MCP_SDK_SOURCES` to reference the new `src/auth/...` paths.
*   `mcp_sdk` builds successfully.

## Step-by-Step Instructions
1.  Create directory `src/auth/`.
2.  Move each required file into `src/auth/` with the required new basename.
3.  Update `CMakeLists.txt` to replace the old `src/auth_*.cpp` paths with the new `src/auth/*.cpp` paths.
4.  Run a full configure and build to validate `mcp_sdk` compiles.

## Verification
*   `cmake --preset vcpkg-unix-release`
*   `cmake --build build/vcpkg-unix-release`
