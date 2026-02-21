# MCP Specification Alignment

## Scope

This document will map this plan to the MCP 2025-11-25 official specification.

## Source Of Truth

* `.docs/requirements/mcp-spec-2025-11-25/spec/basic/transports.md`
* `.docs/requirements/cpp-mcp-sdk.md`

## Streamable HTTP: POST Requirements

* The implementation will send one JSON-RPC message per HTTP POST request.
  - Plan coverage: `task-002`, `task-003`, `task-010`
* The implementation will send `Accept: application/json, text/event-stream` on HTTP POST requests.
  - Plan coverage: existing implementation; regression coverage will remain in `tests/transport_http_client_test.cpp` via `task-002` and `task-003`
* The implementation will support `application/json` responses and `text/event-stream` responses.
  - Plan coverage: existing implementation; concurrency and retry fixes will be implemented by `task-002` and `task-003`

## Streamable HTTP: GET Listen Requirements

* The implementation will open an SSE stream via HTTP GET to the MCP endpoint with `Accept: text/event-stream`.
  - Plan coverage: `task-004`
* The implementation will treat HTTP 405 on GET as a supported configuration and will keep POST functionality operational.
  - Plan coverage: `task-004`
* The implementation will handle inbound JSON-RPC requests and notifications delivered on the GET SSE stream.
  - Plan coverage: `task-004`, `task-005`
* The implementation will post JSON-RPC responses for server-initiated requests back to the server via HTTP POST.
  - Plan coverage: `task-004`, `task-005`

## Retry And Resumption Requirements

* The implementation will respect SSE `retry` guidance by delaying reconnect attempts.
  - Plan coverage: `task-002`
* The implementation will persist SSE event IDs and will send `Last-Event-ID` on subsequent GET polling requests.
  - Plan coverage: existing implementation; listen-loop integration and tests are covered by `task-004` and `task-005`

## Session And Protocol Header Requirements

* The implementation will replay `MCP-Session-Id` on all HTTP requests after a successful initialization response includes a session ID.
  - Plan coverage: `task-008`
* The implementation will replay `MCP-Protocol-Version` on all HTTP requests after protocol negotiation.
  - Plan coverage: `task-008`
* The implementation will clear session header state on HTTP 404 and will require a new initialization lifecycle to re-establish a session.
  - Plan coverage: `task-010`

## Concurrency And Lifecycle Requirements

* The implementation will support concurrent POST request traffic and GET listen traffic without data races.
  - Plan coverage: `task-003`
* The implementation will contain background thread errors and will support clean termination.
  - Plan coverage: `task-009`, `task-004`
