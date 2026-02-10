# Task ID: [task-032]
# Task Name: [Conformance: Transports (stdio + Streamable HTTP + TLS)]

## Context
Validate transport framing rules and Streamable HTTP semantics including headers, status codes, SSE behavior, and TLS.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Transport framing rules; headers; session behavior)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`

## Output / Definition of Done
* `tests/conformance/test_stdio_transport.cpp` covers newline framing and stdout-only messages
* `tests/conformance/test_streamable_http_transport.cpp` covers:
   - POST one-message semantics
   - 202 Accepted for notifications/responses
   - GET SSE listen and 405 behavior
   - Last-Event-ID resumability
   - SSE priming event (id + empty data) behavior
   - `MCP-Session-Id` and `MCP-Protocol-Version` rules
   - session missing (when required) -> 400; terminated session -> 404; client reinit behavior
   - `MCP-Protocol-Version` missing fallback to `2025-03-26` only when server cannot otherwise identify version
   - Origin validation 403
   - HTTPS handshake and verification

## Step-by-Step Instructions
1. Implement in-process HTTP test server/client fixtures.
2. Add SSE parser/encoder test vectors.
3. Add TLS test cert generation and use in tests.
4. Ensure tests run on Windows (avoid POSIX-only assumptions).

## Verification
* `ctest --test-dir build -R transport`
