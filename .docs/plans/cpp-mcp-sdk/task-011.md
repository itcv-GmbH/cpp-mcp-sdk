# Task ID: [task-011]
# Task Name: [Implement Streamable HTTP Server (POST + GET SSE, resumability)]

## Context
Implement the server side of Streamable HTTP with a single endpoint supporting POST and GET, including SSE streaming and resumability rules.

## Inputs
* `.docs/requirements/cpp-mcp-sdk.md` (Streamable HTTP requirements; SSE semantics; Multiple streams; Resumption)
* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`

## Output / Definition of Done
* `src/transport/http_server.cpp` implements:
   - POST: accept one JSON-RPC message
   - if request: reply JSON or SSE stream
   - if notification/response: return 202 Accepted (or error)
   - GET: optionally open SSE stream (or 405)
   - DELETE: optional session termination behavior
* `include/mcp/http/sse.hpp` defines SSE encoder + event ID policy
* Tests cover:
   - POST request -> JSON response
   - POST request -> SSE stream with pre-response messages
   - GET stream -> server-initiated notifications
   - event ID + Last-Event-ID resume rules (no cross-stream replay)
   - SSE priming event (id + empty data) is emitted at stream start
   - session termination/expiry -> HTTP 404 for that session

## Step-by-Step Instructions
1. Implement MCP endpoint routing for POST/GET/DELETE.
   - enforce single endpoint path supporting POST and GET
   - for POST notifications/responses: return 202 with no body on accept
2. Implement SSE framing and event ID generation:
    - event IDs are per-stream cursors; MUST NOT be replayed across streams
    - encode enough to correlate `Last-Event-ID` to the correct stream
    - on SSE start, SHOULD send an event with an event ID and empty `data` to prime reconnection
3. Implement message dispatch to exactly one stream (no broadcast).
4. Implement optional stream closing behavior with `retry` guidance.
   - disconnection MUST NOT be interpreted as request cancellation
5. Implement Last-Event-ID resume (always via HTTP GET):
    - map `Last-Event-ID` to correct stream
    - replay only messages for that stream
    - MUST NOT replay messages destined for other streams
6. Ensure the SSE stream eventually delivers the JSON-RPC response for the originating POST request.
   - after delivering that response on the POST-initiated SSE stream, server SHOULD terminate that SSE stream
7. Add conformance tests for status codes, content types, session 400/404 behavior, and SSE priming.

## Verification
* `ctest --test-dir build`
