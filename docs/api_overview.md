# MCP C++ SDK API Overview

This document defines the initial public API surface and module boundaries for the SDK.

## Module Boundaries

- JSON-RPC core:
  - `include/mcp/jsonrpc/messages.hpp` contains message-level types for requests, notifications, and responses.
  - `include/mcp/jsonrpc/router.hpp` contains method-based handler registration and dispatch contracts.
- Lifecycle session:
  - `include/mcp/lifecycle/session.hpp` contains `mcp::Session`, lifecycle state, outbound request APIs, and transport binding.
- Schema validation:
  - `include/mcp/schema/validator.hpp` contains pinned MCP schema loading and validation APIs for method messages and tool schemas.
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
- Server runners (high-level transport orchestration):
  - `include/mcp/server/runners.hpp` provides access to all runner types via convenience includes.
  - `include/mcp/server/stdio_runner.hpp` defines `mcp::StdioServerRunner` for STDIO transport.
  - `include/mcp/server/streamable_http_runner.hpp` defines `mcp::StreamableHttpServerRunner` for Streamable HTTP.
  - `include/mcp/server/combined_runner.hpp` defines `mcp::CombinedServerRunner` for multi-transport servers.

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

## Server Runners

Server runners provide high-level APIs for running MCP servers over various transports with minimal boilerplate. They encapsulate the transport lifecycle and integrate with the `Server` class for protocol handling.

### Runner Types

| Runner | Header | Use Case |
|--------|--------|----------|
| `mcp::StdioServerRunner` | `include/mcp/server/stdio_runner.hpp` | CLI tools, local processes |
| `mcp::StreamableHttpServerRunner` | `include/mcp/server/streamable_http_runner.hpp` | HTTP clients, web integrations |
| `mcp::CombinedServerRunner` | `include/mcp/server/combined_runner.hpp` | Servers supporting multiple transports |

### STDIO Runner

The STDIO runner provides a blocking `run()` method that reads JSON-RPC messages from stdin and writes responses to stdout. Log messages are written to stderr by default to avoid polluting the JSON-RPC protocol stream.

```cpp
#include <mcp/server/stdio_runner.hpp>

mcp::StdioServerRunner runner;
runner.server()->registerTool(/* ... */);
runner.run();
```

Custom streams can be specified:
```cpp
runner.run(customInput, customOutput, customError);
```

Options can be configured via `StdioServerRunnerOptions`:
```cpp
mcp::StdioServerRunnerOptions options;
options.transportOptions.allowStderrLogs = true;
options.transportOptions.limits.maxJsonRpcMessageSize = 1024 * 1024;
mcp::StdioServerRunner runner(options);
```

### Streamable HTTP Runner

The HTTP runner provides `start()`/`stop()` methods for non-blocking operation. It manages an underlying `mcp::transport::HttpServerRuntime` and handles SSE streaming for notifications.

```cpp
#include <mcp/server/streamable_http_runner.hpp>

mcp::StreamableHttpServerRunner runner;
runner.server()->registerTool(/* ... */);
runner.start();
std::cout << "Running on port " << runner.localPort() << std::endl;
// ... handle requests ...
runner.stop();
```

For multi-client deployments, enable session IDs:
```cpp
options.transportOptions.http.requireSessionId = true;
```

This ensures each client receives a unique session ID, enabling proper request routing and isolation. See task-002 for the complete session isolation contract.

### Combined Runner

The combined runner supports running multiple transports simultaneously:

```cpp
mcp::CombinedServerRunnerOptions options;
options.enableStdio = true;
options.enableHttp = true;
options.httpOptions.transportOptions.http.requireSessionId = true;

mcp::CombinedServerRunner runner(options);
runner.server()->registerTool(/* ... */);
runner.start();  // Starts HTTP in background, runs STDIO in foreground
runner.stop();   // Stops HTTP
```

### Relationship to Transport Primitives

Runners wrap the lower-level transport types:
- `StdioServerRunner` uses `mcp::transport::StdioTransport::run()` internally.
- `StreamableHttpServerRunner` owns `mcp::transport::HttpServerRuntime` and delegates to `mcp::transport::http::StreamableHttpServer`.
- `CombinedServerRunner` orchestrates multiple runners.

Applications needing direct transport access can use the transport headers directly. Runners provide convenience; transports provide flexibility.
