# Task ID: [task-005]
# Task Name: [Unit Tests: Crypto RNG + Runtime Limits Defaults]

## Context
Add direct unit tests for platform RNG wrapper and runtime limit defaults; these are security and reliability guardrails.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Security; Performance and Reliability; resource exhaustion guardrails)
* `include/mcp/security/crypto_random.hpp`
* `include/mcp/security/limits.hpp`
* `tests/security_crypto_random_test.cpp` (created in `task-001`)
* `tests/security_limits_test.cpp` (created in `task-001`)

## Output / Definition of Done
* `tests/security_crypto_random_test.cpp` covers:
  - `cryptoRandomBytes(0)` returns empty vector
  - `cryptoRandomBytes(n)` returns vector size `n` for a small set of values (e.g., 1, 16, 32, 1024)
  - multiple invocations do not throw
* `tests/security_limits_test.cpp` covers:
  - `RuntimeLimits` defaults equal the documented constants in `limits.hpp`
  - default retry/task TTL limits are sane (non-zero, expected ranges)

## Step-by-Step Instructions
1. Implement size/empty contract tests for `cryptoRandomBytes` without asserting entropy-specific properties (avoid flakiness).
2. Add assertions for default fields in `mcp::security::RuntimeLimits` to match `kDefault*` constants.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_security_crypto_random_test --output-on-failure`
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_security_limits_test --output-on-failure`
