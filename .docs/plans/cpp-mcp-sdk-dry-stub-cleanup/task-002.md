# Task ID: task-002
# Task Name: Add Internal Base64url Helpers + Tests

## Context
The SDK implements base64url (no padding) in multiple places (OAuth PKCE, pagination cursors for list endpoints and tasks). Centralizing reduces duplication and helps ensure the same alphabet/padding rules across security-critical code paths.

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Authorization: OAuth 2.1 + PKCE; Pagination cursors)
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md`
*   Existing base64url implementations in `src/auth_oauth_client.cpp`, `src/server/server.cpp`, `src/util/tasks.cpp`

## Output / Definition of Done
*   `include/mcp/detail/base64url.hpp` added with base64url helpers (unpadded):
    *   `mcp::detail::encodeBase64UrlNoPad(std::string_view bytes) -> std::string`
    *   `mcp::detail::decodeBase64UrlNoPad(std::string_view text) -> std::optional<std::string>`
*   `tests/detail_base64url_test.cpp` added with known vectors + roundtrip tests + invalid input tests.
*   `tests/CMakeLists.txt` updated to build and register `mcp_sdk_detail_base64url_test`.

## Step-by-Step Instructions
1.  Create `include/mcp/detail/base64url.hpp` and implement the url-safe alphabet (`A-Z a-z 0-9 - _`) without `=` padding.
2.  Make decoding strict:
    *   reject remainder length 1 (invalid base64 quantum)
    *   reject characters not in the url-safe alphabet
3.  Add `tests/detail_base64url_test.cpp` with:
    *   roundtrip for arbitrary byte payloads (including NUL bytes)
    *   known-vector sanity checks (e.g., RFC 4648 examples adapted for base64url, unpadded)
    *   invalid inputs (whitespace, `+`/`/`, remainder=1)
4.  Wire the new test executable and `add_test()` entry in `tests/CMakeLists.txt`.

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_detail_base64url_test --output-on-failure`
