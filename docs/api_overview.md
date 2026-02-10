# MCP C++ SDK API Overview

This document defines the initial public API surface and module boundaries for the SDK.

## Module Boundaries

- JSON-RPC core:
  - `include/mcp/jsonrpc/messages.hpp` contains message-level types for requests, notifications, and responses.
  - `include/mcp/jsonrpc/router.hpp` contains method-based handler registration and dispatch contracts.
- Lifecycle session:
  - `include/mcp/lifecycle/session.hpp` contains `mcp::Session`, lifecycle state, outbound request APIs, and transport binding.
- Transports:
  - `include/mcp/transport/transport.hpp` defines the base transport contract (`attach`, `start`, `stop`, `send`).
  - `include/mcp/transport/stdio.hpp` defines stdio transport options and type.
  - `include/mcp/transport/http.hpp` defines Streamable HTTP transport options and type.
- Role facades:
  - `include/mcp/server/server.hpp` defines `mcp::Server` as a server-facing facade over a shared session.
  - `include/mcp/client/client.hpp` defines `mcp::Client` as a client-facing facade over a shared session.
- Auth:
  - `include/mcp/auth/provider.hpp` defines async auth provider and verifier interfaces for HTTP authorization integration.
- Core constants and protocol errors:
  - `include/mcp/version.hpp` defines protocol and SDK version constants and negotiated-version accessors.
  - `include/mcp/errors.hpp` defines structured JSON-RPC error data (`code`, `message`, `data`).

## Ownership Model

- `mcp::Client` and `mcp::Server` are thin facades that own `std::shared_ptr<mcp::Session>`.
- `mcp::Session` owns JSON-RPC routing state and negotiated protocol version state.
- A transport is attached to a session via `Session::attachTransport` and is started/stopped through the session lifecycle APIs.

## Handler and Request Model

- Incoming request handlers are registered as `method -> RequestHandler` through `mcp::jsonrpc::Router` and exposed through `mcp::Session`.
- Outbound requests support two completion styles:
  - `Session::sendRequest(...)` returns `std::future<mcp::jsonrpc::Response>`.
  - `Session::sendRequestAsync(...)` accepts a callback (`ResponseCallback`).
- Notifications are one-way and use `Session::sendNotification(...)`.

## Threading Model

- The session exposes explicit threading policy through `SessionOptions::threading`.
- Handler execution policy is configurable:
  - `HandlerThreadingPolicy::kIoThread`: handlers run on the transport/IO execution context.
  - `HandlerThreadingPolicy::kExecutor`: handlers run on a separate executor.
- Applications may provide a custom executor implementation through `SessionThreading::handlerExecutor`.
- If no custom executor is provided while using `kExecutor`, the implementation is expected to provide a default executor in the session runtime.
