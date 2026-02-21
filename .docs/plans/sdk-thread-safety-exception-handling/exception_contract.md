# Exception Contract

## Purpose

This document defines the exception contract for the MCP C++ SDK to ensure consistent error handling and provide clear expectations for SDK users. It reflects the CURRENT implementation state, not aspirational behavior.

## Scope

This contract covers:
- All public API entrypoints with defined failure behavior
- All user-provided callbacks invoked by the SDK
- All background execution contexts created by the SDK
- Destructors and cleanup operations

## Exception Taxonomy

The SDK uses the following exception types as part of its public contract:

### 1. Standard Library Exceptions

| Exception Type | Usage Context |
|----------------|---------------|
| `std::invalid_argument` | Invalid parameter values passed to public methods (e.g., null session, empty executable path) |
| `std::runtime_error` | General operational failures, JSON parsing errors, transport failures |
| `std::logic_error` | Programming errors detected at runtime (e.g., pagination cycle detected) |
| `std::bad_alloc` | Memory allocation failure |

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

#### Noexcept Methods (Actual Declarations)
| Method | Notes |
|--------|-------|
| `~Client()` | Destructor declared noexcept |
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

#### Non-Throwing Methods (Behavior)
| Method | Notes |
|--------|-------|
| `~Router()` | Destructor declared noexcept |
| `dispatchNotification()` | Returns void; exceptions are suppressed (implementation detail) |
| `dispatchResponse()` | Returns bool; exceptions are suppressed (implementation detail) |
| `sendNotification()` | Returns void; exceptions suppressed |
| `emitProgress()` | Returns bool; callback exceptions suppressed |

### 3. Session (`mcp::Session`)

#### Throwing Methods
| Method | Exception Types | Condition |
|--------|-----------------|-----------|
| Constructor | None | - |
| `sendRequest()` | `LifecycleError` | Invalid session state for operation |
| `enforceOutboundRequestLifecycle()` | `LifecycleError` | Invalid session state |
| `attachTransport()` | `std::runtime_error` | Transport error |
| `start()`, `stop()` | May throw | No noexcept guarantee in declaration |

#### Noexcept Methods (Actual Declarations)
| Method | Notes |
|--------|-------|
| `state()` noexcept | Simple state accessor |
| `negotiatedProtocolVersion()` noexcept | Simple accessor |
| `role()` noexcept | Simple accessor |
| `supportedProtocolVersions()` | Returns const ref, noexcept in practice |

### 4. Messages (`mcp::jsonrpc`)

#### Throwing Methods
| Method | Exception Types | Condition |
|--------|-----------------|-----------|
| `parseMessage()` | `MessageValidationError` | Invalid JSON or malformed message |
| `parseMessageJson()` | `MessageValidationError` | Invalid message structure |
| `serializeMessage()` | `std::runtime_error` | Serialization failure |

#### Non-Throwing Methods
| Method | Notes |
|--------|-------|
| `toJson()` | Returns JsonValue, noexcept in practice |
| Error factory functions | Return by value, noexcept in practice |

### 5. InboundLoop (`mcp::detail::InboundLoop`)

#### Throwing Methods
| Method | Exception Types | Condition |
|--------|-----------------|-----------|
| Constructor | `std::bad_alloc` | Memory allocation failure |
| `start()` | `std::runtime_error` | Thread creation failure |

#### Non-Throwing Methods
| Method | Notes |
|--------|-------|
| `~InboundLoop()` | Destructor (implicitly noexcept) |
| `stop()` | Sets atomic flag, noexcept in practice |
| `join()` | Thread join with exception suppression |
| `isRunning()` noexcept | Atomic load |

**Important:** The loop body function passed to `InboundLoop` should not throw. Current implementation catches exceptions from the loop body to prevent thread termination.

### 6. HTTP Transport (`mcp::transport`)

#### Throwing Methods
| Method | Exception Types | Condition |
|--------|-----------------|-----------|
| `StreamableHttpClient::send()` | `std::runtime_error` | HTTP error or serialization failure |
| `StreamableHttpClient::openListenStream()` | `std::runtime_error` | Connection failure |
| `HttpClientRuntime::execute()` | `std::runtime_error` | HTTP request failure |
| `HttpServerRuntime::start()` | `std::runtime_error` | Server startup failure |

#### Non-Throwing Methods
| Method | Notes |
|--------|-------|
| `~StreamableHttpServer()` | Destructor |
| `~StreamableHttpClient()` | Destructor |
| `~HttpServerRuntime()` | Destructor |
| `HttpServerRuntime::stop()` noexcept | Safe shutdown |
| `HttpServerRuntime::isRunning()` noexcept | State check |
| `hasActiveListenStream()` noexcept | State check |

### 7. STDIO Transport (`mcp::transport`)

#### Throwing Methods
| Method | Exception Types | Condition |
|--------|-----------------|-----------|
| `StdioTransport::run()` | `std::runtime_error` | I/O error or protocol error |
| `StdioTransport::attach()` | `std::runtime_error` | Attach failure |
| `StdioTransport::spawnSubprocess()` | `std::runtime_error` | Subprocess spawn failure |
| `StdioSubprocess::writeLine()` | `std::runtime_error` | Write failure |
| `StdioSubprocess::readLine()` | `std::runtime_error` | I/O error |

#### Non-Throwing Methods
| Method | Notes |
|--------|-------|
| `~StdioSubprocess()` | Destructor |
| `StdioSubprocess::closeStdin()` noexcept | Safe close |
| `StdioSubprocess::valid()` noexcept | State check |
| `StdioSubprocess::exitCode()` | Returns optional, noexcept in practice |
| `StdioSubprocess::shutdown()` noexcept | Returns bool success/failure |

## Protocol Error Mapping

### When Errors are JSON-RPC Responses vs C++ Exceptions

| Scenario | Result Type | Example |
|----------|-------------|---------|
| Malformed JSON received | **C++ Exception** (`MessageValidationError`) | `parseMessage("not json")` |
| Valid JSON-RPC with unknown method | **JSON-RPC Response** (`ErrorResponse` with code -32601) | Handler returns error response |
| Valid JSON-RPC with invalid params | **JSON-RPC Response** (`ErrorResponse` with code -32602) | Handler returns error response |
| Transport connection failure | **C++ Exception** (`std::runtime_error`) | `connectHttp()` fails |
| Handler throws exception | **JSON-RPC Response** (`ErrorResponse` with code -32603) | Exception caught, converted to response |
| Server returns error response | **JSON-RPC Response** (`ErrorResponse`) | Normal protocol behavior |

### JSON-RPC Error Codes

| JSON-RPC Error Code | Usage |
|---------------------|-------|
| `-32700` Parse Error | Thrown as `MessageValidationError` when receiving invalid JSON |
| `-32600` Invalid Request | Thrown as `MessageValidationError` when message structure is wrong |
| `-32601` Method Not Found | Returned in `ErrorResponse` when handler not registered |
| `-32602` Invalid Params | Returned in `ErrorResponse` by handler for bad parameters |
| `-32603` Internal Error | Returned in `ErrorResponse` when handler throws exception |
| `-32002` Resource Not Found | Returned in `ErrorResponse` for missing tool/resource |
| `-32042` URL Elicitation Required | Returned in `ErrorResponse` for URL elicitation flow |

### Decision Rule

**Use C++ Exceptions for:**
1. Programming errors (`std::invalid_argument` for bad parameters)
2. System-level failures (`std::runtime_error` for transport/memory issues)
3. Protocol parsing failures (`MessageValidationError` for malformed JSON)

**Use JSON-RPC Error Responses for:**
1. Method-level failures (unknown method, invalid params)
2. Application-level errors (resource not found)
3. Handler exceptions (converted to Internal Error response)

## Callback Exception Behavior

### User-Provided Callbacks

The SDK handles exceptions from user-provided callbacks as follows:

#### Request Handlers
```cpp
using RequestHandler = std::function<std::future<Response>(const RequestContext &, const Request &)>;
```
- **Behavior:** Handler exceptions may propagate through the returned future
- **SDK Response:** If a handler throws, the exception may be caught and converted to an ErrorResponse with JSON-RPC error code -32603 (Internal Error)

#### Notification Handlers
```cpp
using NotificationHandler = std::function<void(const RequestContext &, const Notification &)>;
```
- **Behavior:** Current implementation catches exceptions internally and continues dispatch
- **User Responsibility:** Do not rely on SDK containment; wrap your handler in try-catch

#### Progress Callbacks
```cpp
using ProgressCallback = std::function<void(const RequestContext &, const ProgressUpdate &)>;
```
- **Behavior:** Current implementation catches exceptions internally and continues
- **User Responsibility:** Do not rely on SDK containment; wrap your callback in try-catch

#### Transport Callbacks
- **Inbound message handlers:** Current implementation catches exceptions; malformed messages may be dropped
- **User Responsibility:** Do not rely on SDK containment for error reporting

## Background Execution Context Rules

### Thread Pool Work

Work posted to `boost::asio::thread_pool` by the SDK:
- Current implementation catches exceptions to prevent thread termination
- **User Responsibility:** Do not rely on SDK containment; handle exceptions in your posted work

```cpp
// RECOMMENDED: Wrap your posted work in try-catch
boost::asio::post(pool, []() {
    try {
        // Your code here
    } catch (const std::exception& e) {
        // Handle error appropriately
    }
});
```

### InboundLoop Threads

The `InboundLoop` class provides a unified abstraction for transport reader threads:
- **Loop body:** Current implementation catches exceptions from the loop body function to prevent thread termination, but users should write loop bodies that handle their own exceptions
- **Clean shutdown:** `stop()` and `join()` provide safe shutdown
- **Thread safety:** `isRunning()` is safe to call from any thread

### Destructor Rules

SDK destructors provide basic cleanup:
- **Client::~Client()** - Declared noexcept
- **Router::~Router()** - Declared noexcept
- **Session::~Session()** - Implicitly noexcept (default destructor)
- **InboundLoop::~InboundLoop()** - Implicitly noexcept
- **StreamableHttpServer::~StreamableHttpServer()** - Standard destructor
- **StreamableHttpClient::~StreamableHttpClient()** - Standard destructor
- **HttpServerRuntime::~HttpServerRuntime()** - Standard destructor
- **StdioSubprocess::~StdioSubprocess()** - Standard destructor

**Rule:** Destructors should not throw. Cleanup failures are handled internally or ignored.

## Exception Safety Guarantees

### Basic Guarantee

All SDK methods provide the basic exception safety guarantee:
- The program remains in a valid state
- No resources are leaked (via RAII)
- Invariants are maintained

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
            // Log or handle explicitly - exceptions are suppressed by SDK
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
| 1.1 | 2025-02-22 | Remediated per senior review: removed aspirational claims, clarified actual callback containment behavior, improved protocol mapping clarity |
