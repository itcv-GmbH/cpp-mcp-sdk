# Task ID: [task-001]
# Task Name: [Scaffold New Unit Test Targets]

## Context
Create and wire new unit test executables so subsequent tasks can add test content without repeatedly editing `tests/CMakeLists.txt` (reduces merge conflicts and enables parallel work).

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Interoperability / Conformance Testing; Security; Reliability)
* `tests/CMakeLists.txt` (existing test wiring pattern)

## Output / Definition of Done
* `tests/CMakeLists.txt` builds and registers new test executables:
  - `mcp_sdk_test_util_cancellation`
  - `mcp_sdk_test_util_progress`
  - `mcp_sdk_test_schema_pinned_schema`
  - `mcp_sdk_test_sdk_version`
  - `mcp_sdk_test_security_crypto_random`
  - `mcp_sdk_test_security_limits`
  - `mcp_sdk_test_transport_http_runtime`
  - `mcp_sdk_test_auth_oauth_client_disabled`
* New test source files exist (with non-failing placeholder `TEST_CASE`s) at:
  - `tests/util_cancellation_test.cpp`
  - `tests/util_progress_test.cpp`
  - `tests/schema_pinned_schema_test.cpp`
  - `tests/sdk_version_test.cpp`
  - `tests/security_crypto_random_test.cpp`
  - `tests/security_limits_test.cpp`
  - `tests/transport_http_runtime_test.cpp`
  - `tests/auth_oauth_client_disabled_test.cpp`
* `ctest -N` lists the new tests in the build directory.

## Step-by-Step Instructions
1. Create the new test source files listed above with:
   - `#include <catch2/catch_test_macros.hpp>`
   - The minimum required SDK includes for that module
   - At least one `TEST_CASE` that always passes (placeholder)
2. Update `tests/CMakeLists.txt`:
   - Add `add_executable(...)` for each new test file
   - Link each against `Catch2::Catch2WithMain` and `mcp::sdk`
   - Add corresponding `add_test(NAME ... COMMAND ...)` entries using the established naming style
3. Keep all tests deterministic (no sleeps, no external network).

## Verification
* `cmake --preset vcpkg-unix-release`
* `cmake --build build/vcpkg-unix-release`
* `ctest --test-dir build/vcpkg-unix-release -N`
