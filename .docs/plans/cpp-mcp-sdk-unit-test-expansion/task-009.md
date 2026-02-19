# Task ID: [task-009]
# Task Name: [Expand Unit Tests: Lifecycle State Machine]

## Context
Increase lifecycle test coverage for invalid ordering, capability negotiation edge cases, and pre-initialization constraints.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Lifecycle Management; Versioning and Negotiation)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/lifecycle.md`
* `include/mcp/lifecycle/session.hpp`
* `src/lifecycle/session.cpp`
* `tests/lifecycle_test.cpp`

## Output / Definition of Done
* `tests/lifecycle_test.cpp` adds tests for:
  - server pre-init restrictions (only allowed notifications/requests per spec)
  - client restriction: only `ping` allowed while waiting for initialize response
  - negotiation failure surfaces actionable details (requested vs supported versions)
  - `experimental` capabilities passthrough behavior remains stable

## Step-by-Step Instructions
1. Add negative tests for invalid client-side call ordering (`initialize` not first; normal request while initializing; notification before initialized).
2. Add tests for server-side behavior when receiving non-`initialize` requests in Created state.
3. Add a version negotiation failure test:
   - client proposes unsupported version
   - server returns an error response
   - test asserts error message includes both requested and supported versions.
4. Avoid adding tests that depend on TODO behavior in `Session::sendRequest` transport wiring.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_lifecycle_test --output-on-failure`
