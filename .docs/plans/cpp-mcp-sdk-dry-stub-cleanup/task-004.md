# Task ID: task-004
# Task Name: Add Initialize/Capabilities JSON Codec + Tests

## Context
`Implementation`, `Icon`, and capabilities JSON encoding/decoding exists in multiple places (lifecycle session + client). A shared codec reduces duplication and prevents subtle mismatches between what is sent in `initialize` and what the lifecycle state machine records.

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Lifecycle; Initialize negotiation; Capability negotiation)
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/lifecycle.md`
*   Current implementations in `src/lifecycle/session.cpp` and `src/client/client.cpp`

## Output / Definition of Done
*   `include/mcp/detail/initialize_codec.hpp` added with shared helpers:
    *   `mcp::detail::iconToJson(const mcp::Icon &) -> jsoncons::json`
    *   `mcp::detail::implementationToJson(const mcp::Implementation &) -> jsoncons::json`
    *   `mcp::detail::parseImplementation(const jsoncons::json &, defaults...) -> mcp::Implementation`
    *   `mcp::detail::clientCapabilitiesToJson(const mcp::ClientCapabilities &) -> jsoncons::json`
    *   `mcp::detail::serverCapabilitiesToJson(const mcp::ServerCapabilities &) -> jsoncons::json`
    *   `mcp::detail::parseClientCapabilities(const jsoncons::json &) -> mcp::ClientCapabilities`
    *   `mcp::detail::parseServerCapabilities(const jsoncons::json &) -> mcp::ServerCapabilities`
*   `tests/detail_initialize_codec_test.cpp` added with roundtrip and defaulting tests.
*   `tests/CMakeLists.txt` updated to build and register `mcp_sdk_detail_initialize_codec_test`.

## Step-by-Step Instructions
1.  Create `include/mcp/detail/initialize_codec.hpp` and move the JSON encode/decode logic out of `src/lifecycle/session.cpp` and `src/client/client.cpp` (as shared helpers).
2.  Ensure the codec preserves existing behavior:
    *   omits optional fields when not present
    *   supports `elicitation: {}` meaning form-mode per SRS
    *   preserves `experimental` objects without interpretation
3.  Add unit tests in `tests/detail_initialize_codec_test.cpp`:
    *   Icon/Implementation JSON encode includes only set optionals
    *   Capabilities JSON encodes expected shapes for tasks requests, sampling/tools/context, etc.
    *   Parse functions tolerate missing/empty objects consistent with current behavior
4.  Register the test target in `tests/CMakeLists.txt`.

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_detail_initialize_codec_test --output-on-failure`
