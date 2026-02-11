# Task ID: [task-040]
# Task Name: [Runtime Limits + Backpressure Knobs]

## Context
Add configurable limits to mitigate resource exhaustion and provide backpressure behavior for streaming transports.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Performance/Reliability limits; Security guardrails)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/security_best_practices.md`

## Output / Definition of Done
* `include/mcp/security/limits.hpp` defines configurable limits:
  - max message size
  - max concurrent in-flight requests
  - max SSE stream duration/messages
  - retry caps
  - max concurrent tasks per auth context
* Limits enforced in stdio + HTTP transports and tasks store
* Tests cover limit enforcement and error reporting

## Step-by-Step Instructions
1. Define limits structure and defaults.
2. Enforce limits in parsing (max JSON size) and router (max in-flight).
3. Enforce SSE limits (max buffered messages; reconnect caps).
4. Enforce tasks limits (ttl caps; max active tasks).
5. Add tests for each limit.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R limits --output-on-failure`
