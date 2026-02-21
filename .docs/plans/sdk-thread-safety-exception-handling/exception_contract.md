# Exception Contract

## Purpose

This document defines the exception contract for the MCP C++ SDK to ensure consistent error handling, prevent process termination from exception escape across thread and callback boundaries, and provide clear expectations for SDK users.

## Scope

This contract covers:
- All public API entrypoints that have defined failure behavior
- All user-provided callbacks invoked by the SDK
- All background execution contexts created by the SDK (`std::thread`, `mcp::detail::InboundLoop`, and `boost::asio::thread_pool` work)
- Destructors and cleanup operations

## Exception Taxonomy

The SDK uses the following exception types as part of its public contract:

### 1. Standard Library Exceptions

| Exception Type | Usage Context |
|----------------|---------------|
| `std::invalid_argument` | Invalid parameter values passed to public methods (e.g., null session, empty executable path) |
| `std::runtime_error` | General operational failures, JSON parsing errors, schema validation failures |
| `std::logic_error` | Programming errors detected at runtime (e.g., pagination cycle detected) |

### 2. SDK-Specific Exceptions

| Exception Type | Usage Context | Header |
|----------------|---------------|--------|
| `mcp::jsonrpc::MessageValidationError` | JSON-RPC message parsing/validation failures | `<mcp/jsonrpc/messages.hpp>` |
| `mcp::LifecycleError` | Invalid session lifecycle state transitions | `<mcp/lifecycle/session.hpp>` |
| `mcp::CapabilityError` | Capability negotiation failures or missing required capabilities | `<mcp/lifecycle/session.hpp>` |

## Exception Specification by Component

### 1. Client (`mcp::Client`)

#### Throwing Methods
| Method | Exception Types | Condition |
|--------|-----------------|-----------|
| `Client(std::shared_ptr<Session>)` | `std::invalid_argument` | If session is null |
| `create()` | None (returns null on failure) | - |
| `attachTransport()` | `std::runtime_error` | Transport already attached or transport error |
| `connectStdio()` | `std::invalid_argument`, `std::runtime_error` | Invalid options or subprocess spawn failure |
| `connectHttp()` | `std::invalid_argument`, `std::runtime_error` | Invalid options or connection failure |
| `initialize()` | `std::runtime_error` | Session error or protocol failure |
| `listTools()`, `callTool()`, etc. | `CapabilityError`, `std::runtime_error` | Missing capability or request failure |
| `forEachPage()`, `collectAllPages()` | `std::runtime_error` | Pagination cycle or limit exceeded |

#### Noexcept Methods
| Method | Notes |
|--------|-------|
| `~Client()` noexcept | Destructor never throws |
| `session()` noexcept | Simple accessor |
| `negotiatedProtocolVersion()` noexcept | Simple accessor |

### 2. Router (`mcp::jsonrpc::Router`)

#### Throwing Methods
| Method | Exception Types | Condition |
|--------|-----------------|-----------|
| Constructor | None | - |
| `registerRequestHandler()`, `registerNotificationHandler()` | None | Handlers are stored as-is |
| `dispatchRequest()` | Handler exceptions propagated | User handler exceptions escape here |
| `sendRequest()` | `std::runtime_error` | Transport or serialization failure |

#### Noexcept Methods
| Method | Notes |
|--------|-------|
| `~Router()` noexcept | Destructor never throws |
| `dispatchNotification()` | void-returning, exceptions contained internally |
| `dispatchResponse()` | Returns bool, exceptions contained |

### 3. Session (`mcp::Session`)

#### Throwing Methods
| Method | Exception Types | Condition |
|--------|-----------------|-----------|
| Constructor | None | - |
| `sendRequest()` | `LifecycleError` | Invalid session state for operation |
| `enforceOutboundRequestLifecycle()` | `LifecycleError` | Invalid session state |
| `attachTransport()` | `std::runtime_error` | Transport error |
| `start()`, `stop()` | `std::runtime_error` | Lifecycle error |

#### Noexcept Methods
| Method | Notes |
|--------|-------|
| `state()` noexcept | Simple state accessor |
| `negotiatedProtocolVersion()` noexcept | Simple accessor |
| `supportedProtocolVersions()` | Returns const ref, no allocations |

### 4. Messages (`mcp::jsonrpc`)

#### Throwing Methods
| Method | Exception Types | Condition |
|--------|-----------------|-----------|
| `parseMessage()` | `MessageValidationError` | Invalid JSON or malformed message |
| `parseMessageJson()` | `MessageValidationError` | Invalid message structure |
| `serializeMessage()` | `std::runtime_error` | Serialization failure |

#### Noexcept Methods
| Method | Notes |
|--------|-------|
| `toJson()` noexcept | Returns JsonValue by value |
| Error factory functions | Return by value, no throw |

### 5. InboundLoop (`mcp::detail::InboundLoop`)

#### Throwing Methods
| Method | Exception Types | Condition |
|--------|-----------------|-----------|
| Constructor | `std::bad_alloc` | Memory allocation failure |
| `start()` | `std::runtime_error` | Thread creation failure |

#### Noexcept Methods
| Method | Notes |
|--------|-------|
| `~InboundLoop()` noexcept | Destructor never throws |
| `stop()` noexcept | Sets atomic flag only |
| `join()` noexcept | Thread join with exception containment |
| `isRunning()` noexcept | Atomic load |

**Critical Invariant:** The loop body function passed to `InboundLoop` **must not throw**. All exceptions thrown by the loop body are caught and suppressed to prevent thread termination.

### 6. HTTP Transport (`mcp::transport`)

#### Throwing Methods
| Method | Exception Types | Condition |
|--------|-----------------|-----------|
| `StreamableHttpClient::send()` | `std::runtime_error` | HTTP error or serialization failure |
| `StreamableHttpClient::openListenStream()` | `std::runtime_error` | Connection failure |
| `HttpClientRuntime::execute()` | `std::runtime_error` | HTTP request failure |
| `HttpServerRuntime::start()` | `std::runtime_error` | Server startup failure |

#### Noexcept Methods
| Method | Notes |
|--------|-------|
| `~StreamableHttpServer()` noexcept | Destructor never throws |
| `~StreamableHttpClient()` noexcept | Destructor never throws |
| `~HttpServerRuntime()` noexcept | Destructor never throws |
| `HttpServerRuntime::stop()` noexcept | Safe shutdown |
| `HttpServerRuntime::isRunning()` noexcept | Atomic/state check |
| `hasActiveListenStream()` noexcept | State check |

### 7. STDIO Transport (`mcp::transport`)

#### Throwing Methods
| Method | Exception Types | Condition |
|--------|-----------------|-----------|
| `StdioTransport::run()` | `std::runtime_error` | I/O error or protocol error |
| `StdioTransport::attach()` | `std::runtime_error` | Attach failure |
| `StdioTransport::spawnSubprocess()` | `std::runtime_error` | Subprocess spawn failure |
| `StdioSubprocess::writeLine()` | `std::runtime_error` | Write failure |
| `StdioSubprocess::shutdown()` noexcept | Returns bool success/failure |

#### Noexcept Methods
| Method | Notes |
|--------|-------|
| `~StdioSubprocess()` noexcept | Destructor never throws |
| `StdioSubprocess::closeStdin()` noexcept | Safe close |
| `StdioSubprocess::valid()` noexcept | State check |
| `StdioSubprocess::exitCode()` | Returns optional, no throw |

## Protocol Error Mapping

### JSON-RPC Error Codes to C++ Exceptions

The SDK maintains a clear separation between protocol-level errors and C++ exceptions:

| JSON-RPC Error Code | C++ Representation | Context |
|---------------------|-------------------|---------|
| `-32700` Parse Error | `MessageValidationError` | Thrown by `parseMessage()` |
| `-32600` Invalid Request | `MessageValidationError` | Thrown by `parseMessage()` |
| `-32601` Method Not Found | JSON-RPC error response | Returned as `ErrorResponse`, not thrown |
| `-32602` Invalid Params | JSON-RPC error response | Returned as `ErrorResponse`, not thrown |
| `-32603` Internal Error | JSON-RPC error response | Handler exceptions converted to error responses |
| `-32002` Resource Not Found | JSON-RPC error response | Tool/resource operation failure |
| `-32042` URL Elicitation Required | JSON-RPC error response | Returned as `ErrorResponse` |

**Rule:** JSON-RPC protocol errors that have a corresponding response message are returned as `ErrorResponse` objects, not thrown as C++ exceptions. C++ exceptions are used for:
1. Programming errors (invalid arguments)
2. System-level failures (transport errors, memory exhaustion)
3. Protocol parsing failures (malformed messages)

## Callback Exception Containment

### User-Provided Callbacks

The SDK treats all user-provided callbacks as potentially throwing. The containment rules are:

#### Request Handlers
```cpp
using RequestHandler = std::function<std::future<Response>(const RequestContext &, const Request &)>;
```
- **Containment:** Handler exceptions are caught and converted to JSON-RPC error responses
- **Correlation ID preservation:** The original request ID is always preserved in the error response
- **Error code:** Internal errors use `-32603` (Internal Error)

#### Notification Handlers
```cpp
using NotificationHandler = std::function<void(const RequestContext &, const Notification &)>;
```
- **Containment:** Handler exceptions are caught and suppressed
- **Reporting:** Failures are reported via unified error reporting (if configured)
- **No propagation:** Exceptions never escape the notification dispatch context

#### Progress Callbacks
```cpp
using ProgressCallback = std::function<void(const RequestContext &, const ProgressUpdate &)>;
```
- **Containment:** Callback exceptions are caught and suppressed
- **No propagation:** Exceptions never propagate to the caller

#### Transport Callbacks
- **Inbound message handlers:** Exceptions are caught and suppressed; malformed messages are dropped
- **Error handlers:** Wrapped in catch-all boundaries

## Background Execution Context Rules

### Thread Pool Work

All work posted to `boost::asio::thread_pool` by the SDK must be noexcept in practice:

```cpp
// Correct: Exception containment within posted work
boost::asio::post(pool, []() noexcept {
    try {
        // User code here
    } catch (...) {
        // Handle or report error
    }
});
```

**Guarantee:** The SDK ensures that no exceptions escape thread pool worker threads.

### InboundLoop Threads

The `InboundLoop` class provides a unified abstraction for transport reader threads:
- **Exception containment:** All loop body exceptions are caught and suppressed
- **Clean shutdown:** `stop()` and `join()` are noexcept
- **Thread safety:** `isRunning()` is safe to call from any thread

### Destructor Rules

All SDK destructors are `noexcept`:
- **Client::~Client() noexcept**
- **Router::~Router() noexcept**
- **Session::~Session() noexcept** (implicit)
- **InboundLoop::~InboundLoop() noexcept**
- **StreamableHttpServer::~StreamableHttpServer() noexcept**
- **StreamableHttpClient::~StreamableHttpClient() noexcept**
- **HttpServerRuntime::~HttpServerRuntime() noexcept**
- **StdioSubprocess::~StdioSubprocess() noexcept**

**Critical Rule:** Destructors never throw. Cleanup failures are handled by:
1. Suppressing exceptions internally
2. Returning success/failure status where appropriate
3. Logging errors through the unified error reporting mechanism

## Unified Error Reporting

### Error Reporting Callback

The SDK provides a unified error reporting mechanism for failures that occur on background threads:

```cpp
using ErrorReporter = std::function<void(ErrorContext)>;
```

**Invocation Rules:**
1. The error reporting callback is invocable from any SDK thread
2. The SDK treats the error reporting callback as potentially throwing
3. Every invocation is wrapped in a catch-all boundary
4. Callback failures are suppressed (logged to stderr as fallback)

### Error Context

```cpp
struct ErrorContext {
    std::string component;      // Component where error occurred (e.g., "router", "transport")
    std::string operation;      // Operation being performed (e.g., "dispatch", "send")
    std::string errorMessage;   // Human-readable error message
    std::optional<jsonrpc::RequestId> requestId;  // Associated request ID if applicable
    std::exception_ptr exception;  // Original exception (if any)
};
```

## Exception Safety Guarantees

### Basic Guarantee

All SDK methods provide the basic exception safety guarantee:
- The program remains in a valid state
- No resources are leaked
- Invariants are maintained

### Strong Guarantee

The following methods provide the strong exception safety guarantee (commit-or-rollback):
- `Session::attachTransport()`
- `Client::attachTransport()`
- `Router::registerRequestHandler()`
- `Router::registerNotificationHandler()`

### Noexcept Operations

All noexcept operations are guaranteed to never throw:
- Destructors
- State accessors (`isRunning()`, `state()`, etc.)
- Simple getters returning const references
- Stop/join operations on background threads

## Best Practices for SDK Users

### Writing Exception-Safe Request Handlers

```cpp
router.registerRequestHandler("my/method",
    [](const jsonrpc::RequestContext& ctx, const jsonrpc::Request& req)
        -> std::future<jsonrpc::Response> {
        try {
            // Your implementation
            return makeReadyFuture(successResponse);
        } catch (const MyAppError& e) {
            // Convert application errors to JSON-RPC errors
            return makeReadyFuture(makeErrorResponse(...));
        } catch (const std::exception& e) {
            // Unexpected errors become internal errors
            return makeReadyFuture(makeInternalErrorResponse(...));
        }
    });
```

### Writing Exception-Safe Notification Handlers

```cpp
session.registerNotificationHandler("my/notification",
    [](const jsonrpc::RequestContext& ctx, const jsonrpc::Notification& notif) {
        try {
            // Your implementation
        } catch (...) {
            // Exceptions are automatically contained by the SDK,
            // but you may want to log or handle them explicitly
        }
    });
```

### Handling Transport Errors

```cpp
try {
    client.connectHttp(options);
} catch (const std::invalid_argument& e) {
    // Invalid options - programming error
} catch (const std::runtime_error& e) {
    // Connection failure - may retry
}
```

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-02-21 | Initial exception contract definition |
