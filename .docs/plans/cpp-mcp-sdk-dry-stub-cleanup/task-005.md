# Task ID: task-005
# Task Name: Refactor Pagination Cursor Base64url

## Context
Both server pagination and tasks pagination implement nearly identical base64url cursor encoding/decoding. This task migrates cursor encoding/decoding to the shared base64url helper without changing cursor semantics (opaque cursors; invalid cursor -> `-32602`).

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Pagination semantics; Tasks semantics)
*   `.docs/requirements/mcp-spec-2025-11-25/spec/server/utilities/pagination.md`
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/tasks.md`
*   Existing cursor logic in `src/server/server.cpp` and `src/util/tasks.cpp`
*   Shared helper from `include/mcp/detail/base64url.hpp` (from `task-002`)

## Output / Definition of Done
*   `src/server/server.cpp` updated to use `mcp::detail::encodeBase64UrlNoPad` / `decodeBase64UrlNoPad` for cursor payloads.
*   `src/util/tasks.cpp` updated to use the same helper for its cursor payloads.
*   No behavioral regressions: existing pagination/cursor unit tests continue to pass.

## Step-by-Step Instructions
1.  In `src/server/server.cpp`, replace the local base64url encode/decode implementation with calls to the shared helper.
2.  Preserve existing cursor payload prefixes and endpoint labeling so previously-issued cursors still parse.
3.  In `src/util/tasks.cpp`, replace its local base64url encode/decode implementation with calls to the shared helper.
4.  Ensure invalid cursor handling remains unchanged (return `std::nullopt` / trigger `-32602` paths as before).
5.  If any unit tests rely on cursor *shape*, update them to treat cursors as opaque (preferred) while keeping the existing behavior stable.

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_server_test --output-on-failure`
*   `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_tasks_test --output-on-failure`
