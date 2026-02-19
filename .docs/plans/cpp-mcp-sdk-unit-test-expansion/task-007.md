# Task ID: [task-007]
# Task Name: [Expand Unit Tests: JSON-RPC Messages]

## Context
Increase unit coverage for strict JSON-RPC parsing/serialization behavior and edge-case handling.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (JSON-RPC Message Conformance)
* `include/mcp/jsonrpc/messages.hpp`
* `src/jsonrpc/messages.cpp`
* `tests/jsonrpc_messages_test.cpp`

## Output / Definition of Done
* `tests/jsonrpc_messages_test.cpp` adds tests for:
  - response invariants: exactly one of `result` or `error`
  - notification invariants: must not accept an `id`
  - strict UTF-8 enforcement on parsing (reject invalid byte sequences)
  - `serializeMessage` produces a single-line payload when required by stdio framing expectations
  - malformed JSON produces `ParseError` mapping consistently

## Step-by-Step Instructions
1. Add negative tests for notifications with `id` and for responses containing both `result` and `error`.
2. Add tests that feed invalid UTF-8 strings to `parseMessage` and assert it throws the expected error type.
3. Add tests ensuring `serializeMessage` output does not include `\n` characters.
4. Ensure all new tests align with existing error types already asserted in the suite.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_jsonrpc_messages_test --output-on-failure`
