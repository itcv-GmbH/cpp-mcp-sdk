# Task ID: task-001
# Task Name: Add Internal ASCII Helpers + Tests

## Context
Multiple modules re-implement small ASCII-only primitives (trim, lowercase, case-insensitive compare, whitespace/control detection). These are used in security-sensitive parsing (HTTP headers, Origin policy, OAuth discovery) and should be centralized and unit-tested to prevent drift.

## Inputs
*   `.docs/requirements/cpp-mcp-sdk.md` (Security; HTTP security; Authorization)
*   `.docs/requirements/mcp-spec-2025-11-25/spec/basic/security_best_practices.md`
*   Existing duplicated implementations in `include/mcp/transport/http.hpp`, `include/mcp/security/origin_policy.hpp`, and `src/auth_*.cpp`

## Output / Definition of Done
*   `include/mcp/detail/ascii.hpp` added with locale-independent ASCII helpers:
    *   `mcp::detail::trimAsciiWhitespace(std::string_view) -> std::string_view`
    *   `mcp::detail::toLowerAscii(std::string_view) -> std::string`
    *   `mcp::detail::equalsIgnoreCaseAscii(std::string_view, std::string_view) -> bool`
    *   `mcp::detail::containsAsciiWhitespaceOrControl(std::string_view) -> bool`
*   `tests/detail_ascii_test.cpp` added with coverage for edge cases (empty, all-space, mixed case, non-ASCII bytes treated as bytes).
*   `tests/CMakeLists.txt` updated to build and register `mcp_sdk_detail_ascii_test`.

## Step-by-Step Instructions
1.  Create `include/mcp/detail/ascii.hpp` as a small header-only module.
2.  Ensure implementations use `static_cast<unsigned char>(c)` before calling `<cctype>` functions to avoid UB.
3.  Prefer naming that communicates ASCII semantics (e.g., `equalsIgnoreCaseAscii`) to avoid accidental use for Unicode.
4.  Add `tests/detail_ascii_test.cpp` using Catch2; include cases that mirror existing call-site expectations (header name matching; trimming header values).
5.  Wire the new test into `tests/CMakeLists.txt` as:
    *   `add_executable(mcp_sdk_test_detail_ascii detail_ascii_test.cpp)`
    *   `add_test(NAME mcp_sdk_detail_ascii_test COMMAND mcp_sdk_test_detail_ascii)`

## Verification
*   `cmake --preset vcpkg-unix-release && cmake --build build/vcpkg-unix-release && ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_detail_ascii_test --output-on-failure`
