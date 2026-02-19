# Task ID: task-010
# Task Name: Deduplicate Client Initialize JSON Building

## Context
The client builds `initialize` params (clientInfo/capabilities/protocolVersion) using local JSON helpers, while lifecycle parsing/serialization exists elsewhere. This task makes client-side initialize-building use the shared initialize codec to avoid mismatch.

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Lifecycle ordering; Initialize defaults)
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/lifecycle.md`
*   Client initialize code in `src/client/client.cpp`
*   Shared helpers from `include/mcp/detail/initialize_codec.hpp` (from `task-004`)

## Output / Definition of Done
*   `src/client/client.cpp` updated to use the shared initialize/capabilities codec helpers instead of local `implementationToJson`, `iconToJson`, `clientCapabilitiesToJson`.
*   Redundant local helper functions removed from `src/client/client.cpp`.
*   Client unit tests and lifecycle conformance tests pass.

## Step-by-Step Instructions
1.  Replace local JSON builder functions in `src/client/client.cpp` with calls into the shared codec.
2.  Ensure defaults remain consistent with SRS:
    *   default protocol version uses the latest supported version
    *   default clientInfo name/version are set when missing
3.  Confirm that `initialize` messages produced before/after the refactor are structurally identical (except for object key ordering, which is non-normative).
4.  Remove the now-unused local helper functions from the file.

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_client_test --output-on-failure`
*   `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_conformance_lifecycle_test --output-on-failure`
