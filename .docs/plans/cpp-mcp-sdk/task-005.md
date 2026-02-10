# Task ID: [task-005]
# Task Name: [Implement Router + In-Flight Requests + Timeouts/Cancellation/Progress]

## Context
Provide bidirectional request routing with concurrent in-flight requests, response correlation, configurable timeouts, and shared utility plumbing.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Bidirectional Operation; Lifecycle timeouts; Cancellation; Progress)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/cancellation.md`
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/utilities/progress.md`

## Output / Definition of Done
* `include/mcp/jsonrpc/router.hpp` defines:
  - handler registry
  - outbound request API with correlation
  - in-flight map keyed by `RequestId`
* `src/jsonrpc/router.cpp` implements:
  - concurrent awaits
  - timeout -> best-effort `notifications/cancelled` (except initialize)
  - progress token plumbing passthrough
* Tests validate:
  - responses are delivered to correct waiter
  - late responses after cancellation are ignored (per SRS)

## Step-by-Step Instructions
1. Implement handler registration per method name.
2. Implement outbound request send + promise/future completion when response arrives.
3. Add timeout management with timers (Boost.Asio) and cancellation emission rules.
4. Add hooks for progress notifications:
   - allow receiver to emit `notifications/progress`
   - enforce monotonicity and stop-on-completion at the feature layer
5. Ensure router rejects responses to notifications and ignores unknown response IDs.

## Verification
* `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
* `cmake --build build`
* `ctest --test-dir build`
