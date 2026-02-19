# Task ID: [task-017]
# Task Name: [Expand Unit Tests: Protected Resource Metadata + Challenge Parsing]

## Context
Deepen parsing and validation unit tests for `WWW-Authenticate` challenges and RFC9728 protected resource metadata discovery results.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Authorization discovery; SSRF mitigations; security)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/authorization.md`
* `include/mcp/auth/protected_resource_metadata.hpp`
* `src/auth_protected_resource_metadata.cpp`
* `tests/auth_protected_resource_metadata_test.cpp`

## Output / Definition of Done
* `tests/auth_protected_resource_metadata_test.cpp` adds tests for:
  - multiple `WWW-Authenticate` headers with mixed schemes (Basic/Digest/Bearer)
  - parsing of `scope` values with commas vs spaces, and whitespace trimming
  - malformed parameter quoting/escaping produces a safe failure (no crashes)
  - discovery refuses non-HTTPS `resource_metadata` URLs (per policy)
  - redirect policy rejects scheme downgrade and unexpected origin changes

## Step-by-Step Instructions
1. Add Bearer challenge parsing tests for multiple headers and multiple challenges in one header.
2. Add negative tests for malformed quoted strings and missing `resource_metadata` parameter.
3. Add discovery policy tests using the existing mock transport approach in the test file.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_auth_protected_resource_metadata_test_authorization --output-on-failure`
