# Task ID: [task-011]
# Task Name: [Expand Unit Tests: Stdio Transport]

## Context
Deepen stdio transport unit tests for framing rules, UTF-8 validation, parse error reporting, and limits enforcement.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (stdio transport requirements; UTF-8)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md` (stdio section)
* `include/mcp/transport/stdio.hpp`
* `src/transport/stdio.cpp`
* `tests/transport_stdio_test.cpp`

## Output / Definition of Done
* `tests/transport_stdio_test.cpp` adds tests for:
  - rejecting embedded `\n` (already covered for CR/CRLF; add LF-in-body cases)
  - parse error emission behavior when `emitParseErrors=true` (ensures stdout remains MCP-only)
  - max message size handling (if not already covered via `runtime_limits_test.cpp`, add direct stdio framing case)
  - behavior with leading/trailing whitespace lines and empty lines

## Step-by-Step Instructions
1. Add framing tests for embedded LF characters and ensure rejection.
2. Add tests for empty/whitespace-only lines (should be ignored or rejected consistently per implementation).
3. Add tests verifying parse errors (when enabled) are valid JSON-RPC error responses.

## Verification
* `ctest --test-dir build/vcpkg-unix-release -R mcp_sdk_transport_stdio_test --output-on-failure`
