# Task ID: [task-008]
# Task Name: [Implement stdio Transport (Server + Client I/O)]

## Context
Implement newline-delimited JSON-RPC over stdin/stdout with strict framing and stderr logging rules.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Transport Support: stdio)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md` (stdio section)

## Output / Definition of Done
* `include/mcp/transport/stdio.hpp` defines stdio transport adapters
* Server mode reads stdin lines -> routes messages; writes only valid messages to stdout
* Client mode writes valid messages to child stdin and reads stdout lines
* Tests validate:
  - embedded newlines are rejected
  - stdout is MCP-only

## Step-by-Step Instructions
1. Implement a line-framed reader (no embedded newlines) and writer.
2. Ensure UTF-8 encoding for JSON text.
3. Provide server “run” helper that binds router to stdin/stdout.
4. Provide client “attach” helper that binds router to provided streams.
5. Add unit tests with in-memory streams.

## Verification
* `ctest --test-dir build`
