# Task ID: [task-012]
# Task Name: [Implement Streamable HTTP Client (POST + GET SSE, resumability)]

## Context
Implement client behavior for Streamable HTTP: one message per POST, handle JSON or SSE responses, optional GET SSE listen stream, and resumability.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP requirements)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`

## Output / Definition of Done
* `src/transport/http_client.cpp` implements:
   - POST send with `Accept: application/json, text/event-stream`
   - handle 202 for notifications/responses
   - handle JSON response or SSE response
   - optional GET listen stream with reconnect respecting `retry`
   - Last-Event-ID resume behavior (resumption is always via HTTP GET)
* Tests cover:
   - reconnection logic
   - multi-stream routing (messages delivered once)

## Step-by-Step Instructions
1. Implement HTTP request builder:
   - add required Accept headers
   - add session and protocol version headers when applicable
2. Implement SSE client parser (events, id, retry, data).
   - handle initial priming event (id + empty data)
3. Implement POST-response handling:
   - if JSON: parse one response
   - if SSE: process messages until the response for the originating request arrives
4. Implement GET listen stream:
   - reconnect with Last-Event-ID when server provides IDs
   - respect `retry`
   - support multiple concurrent SSE streams (POST-initiated streams and optional GET listen stream)
5. Add tests with a local test server.
   - verify resumption is performed via HTTP GET with `Last-Event-ID`
   - verify disconnections do not cancel requests unless explicit cancellation is sent

## Verification
* `ctest --test-dir build`
