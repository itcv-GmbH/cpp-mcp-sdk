# Task ID: [task-022]
# Task Name: [CI: Add Feature-Matrix Builds + Test Selection]

## Context
Optionally add CI jobs for additional build configurations (auth disabled and/or TLS disabled) and ensure the test suite behaves correctly under those feature flags.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Platform support; deterministic CI)
* `.github/workflows/ci.yml`
* `cmake/Options.cmake` (feature flags)
* `tests/auth_oauth_client_disabled_test.cpp` (auth-disabled assertions)

## Output / Definition of Done
* `.github/workflows/ci.yml` includes at least one additional matrix lane that configures with:
  - `-DMCP_SDK_ENABLE_AUTH=OFF`
* The lane runs a compatible test subset, and explicitly runs the auth-disabled unit tests.
* (Optional) Add a no-OpenSSL lane:
  - `-DMCP_SDK_ENABLE_AUTH=OFF -DMCP_SDK_ENABLE_TLS=OFF`
  - TLS-only tests are guarded or excluded to avoid false failures.

## Step-by-Step Instructions
1. Add a new job or matrix entry in `.github/workflows/ci.yml` for the `AUTH=OFF` configuration.
2. Configure CMake with the additional `-D` flags and build.
3. Run tests with `ctest` using include/exclude regexes so tests that require OAuth flows are not expected to pass when auth is disabled.
4. Ensure at least the following run in the auth-off job:
   - `mcp_sdk_auth_oauth_client_disabled_test`
   - core non-auth unit tests (jsonrpc/lifecycle/transports) that are unaffected
5. If adding the no-OpenSSL lane, update TLS-specific tests to be conditional (`#if MCP_SDK_ENABLE_TLS`).

## Verification
* CI: `.github/workflows/ci.yml` passes on Linux/macOS/Windows
* Local (optional): configure a separate build dir and run `ctest` with matching regexes
