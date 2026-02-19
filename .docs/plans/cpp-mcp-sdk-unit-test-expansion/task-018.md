# Task ID: [task-018]
# Task Name: [Expand Unit Tests: Client Registration]

## Context
Increase coverage for client registration strategy selection, dynamic registration request/response validation, and error handling.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Authorization: SHOULD support CIMD + dynamic registration)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md` (Client Registration Approaches)
* `include/mcp/auth/client_registration.hpp`
* `src/auth_client_registration.cpp`
* `tests/auth_client_registration_test.cpp`

## Output / Definition of Done
* `tests/auth_client_registration_test.cpp` adds tests for:
  - dynamic registration fails on non-201 responses with actionable errors
  - response JSON missing required fields (`client_id`, `redirect_uris`) is rejected
  - `token_endpoint_auth_method` parsing and allowed values
  - CIMD inputs validate https scheme and required metadata fields

## Step-by-Step Instructions
1. Add tests for dynamic registration HTTP executor returning:
   - 400/500 status codes
   - invalid JSON
   - missing required JSON fields
2. Add tests for supported/unsupported auth method strings.
3. Expand CIMD validation tests (bad scheme, missing path, empty client name).

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_auth_client_registration_test_authorization --output-on-failure`
