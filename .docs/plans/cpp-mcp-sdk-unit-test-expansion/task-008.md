# Task ID: [task-008]
# Task Name: [Expand Unit Tests: JSON-RPC Router]

## Context
Increase coverage for router correlation, ignoring invalid/late responses, and cancellation/progress plumbing invariants.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Bidirectional operation; timeouts; cancellation; progress)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/cancellation.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/progress.md`
* `include/mcp/jsonrpc/router.hpp`
* `tests/jsonrpc_router_test.cpp`

## Output / Definition of Done
* `tests/jsonrpc_router_test.cpp` adds tests for:
  - unknown response IDs are ignored (no crash, no waiter completion)
  - response to notification is ignored/rejected
  - cancellation is only emitted for in-flight outbound requests in that direction
  - progress notifications are routed to the correct handler and do not leak between tokens
  - timeout behavior does not attempt to cancel `initialize` (if router/session enforces this)

## Step-by-Step Instructions
1. Add an outbound request correlation test using two simultaneous requests and out-of-order responses.
2. Add a test that dispatches an inbound response for an unknown ID and asserts `dispatchResponse` returns false (or equivalent) and no handlers are triggered.
3. Add tests around cancellation/progress message construction already present in the router tests, expanding negative cases (wrong sender, wrong token type).

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_jsonrpc_router_test --output-on-failure`
