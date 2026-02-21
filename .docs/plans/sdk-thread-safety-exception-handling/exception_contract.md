# Exception Contract

## Purpose

This document must define the exception contract for the SDK.

This contract is required to prevent process termination from exception escape across thread and callback boundaries.

## Scope

- This contract must cover:
  - all public API entrypoints that have defined failure behavior
  - all user-provided callbacks invoked by the SDK
  - all background execution contexts created by the SDK (`std::thread`, `mcp::detail::InboundLoop`, and `boost::asio::thread_pool` work)

## Contract Requirements

- Public methods must document whether they throw exceptions and must document the exception types that are part of the public API contract.
- All destructors must not throw.
- All SDK-created thread entrypoints must contain exceptions and must not allow exceptions to escape into the C++ runtime.
- All work posted to `boost::asio::thread_pool` by the SDK must contain exceptions and must not allow exceptions to escape a pool worker thread.
- Protocol-level failures must be represented as JSON-RPC error responses when the protocol requires a JSON-RPC response.
- Tool execution failures must be represented using the MCP-required result shapes (for example, tool call failures must use `CallToolResult.isError` rather than protocol-level JSON-RPC errors except where the MCP specification requires protocol-level errors).
- Transport-level failures must be represented as C++ exceptions at the transport API boundary.
- Failures that occur on background threads must be reported via the unified error reporting mechanism.

## Callback Exception Containment

- The SDK must treat all user-provided callbacks as potentially throwing.
- The SDK must contain exceptions thrown by user-provided callbacks.
- For request handlers, the SDK must translate callback exceptions into deterministic error responses that preserve the request correlation ID.
- For notification handlers and progress callbacks, the SDK must suppress callback exceptions and must report the failure via unified error reporting.

## Unified Error Reporting Invocation Rules

- The unified error reporting callback type must be invocable from any SDK thread.
- The SDK must treat the error reporting callback as potentially throwing.
- Every invocation of the error reporting callback must be wrapped in a catch-all boundary, and callback failures must be suppressed.
