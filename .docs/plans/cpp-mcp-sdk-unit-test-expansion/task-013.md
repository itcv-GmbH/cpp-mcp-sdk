# Task ID: [task-013]
# Task Name: [Expand Unit Tests: Streamable HTTP Common Validation]

## Context
Add more unit tests for shared Streamable HTTP header/state helpers: case-insensitive header behavior, whitespace trimming, session ID validation, protocol version fallback rules.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP; session + protocol header requirements)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
* `include/mcp/transport/http.hpp`
* `tests/transport_http_common_test.cpp`

## Output / Definition of Done
* `tests/transport_http_common_test.cpp` adds tests for:
  - `setHeader/getHeader` are case-insensitive and overwrite existing values
  - `SessionHeaderState::captureFromInitializeResponse` trims ASCII whitespace and rejects invalid visible-ASCII session IDs
  - `isValidProtocolVersion` rejects malformed dates and non-digit characters
  - protocol fallback behavior only applies when version cannot be inferred (expand scenarios)

## Step-by-Step Instructions
1. Add header-case matrix tests (`MCP-Session-Id` vs `mcp-session-id`, etc.).
2. Add whitespace trim tests for session header values.
3. Add protocol version validation tests for length/dash positions/non-digit.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_transport_http_common_test --output-on-failure`
