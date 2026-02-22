# Thread-Safety Contract

## Purpose

This document defines the thread-safety contract for the MCP C++ SDK.

This contract is required to eliminate undefined behavior under concurrent use and is required to make the concurrency guarantees of the SDK auditable.

## Scope

- This contract must cover every public type and public entrypoint that participates in any of the following:
  - Concurrent in-flight request routing
  - Background thread creation (`std::thread` and `mcp::detail::InboundLoop`)
  - Work scheduling onto `boost::asio::thread_pool`
  - Invocation of user-provided callbacks
- This contract must treat all other public types as `Thread-compatible` unless this document assigns a stronger classification.

## Contract Definitions

### Thread-Safety Classifications

- **Thread-safe**: All documented entrypoints are safe under concurrent invocation from multiple threads. The implementation provides internal synchronization.
- **Thread-compatible**: Concurrent invocation is not supported, and external serialization is required (e.g., via mutex).
- **Thread-confined**: All entrypoints must be called from a single designated thread, and the designated thread will be documented.
- **Thread-unsafe**: No thread-safety guarantees. External synchronization is mandatory.

### Method-Level Concurrency Rules

For each covered type, the contract specifies:
- Which methods are safe to call concurrently
- Which methods require exclusive access
- Which methods must only be called during specific lifecycle phases

### Callback Threading Rules

- Every callback type exposed by the SDK declares whether the SDK invokes callbacks serially or concurrently.
- Every callback type exposed by the SDK declares whether the SDK invokes the callback on an I/O thread, an internal worker thread, or an application-provided executor.

**Note on Threading Policy**: The `SessionOptions::threading` field and `HandlerThreadingPolicy` enum are currently defined in the API but are not utilized by the runtime implementation. Callback threading behavior is fixed as documented below and does not currently respect the threading policy configuration.

### Lifecycle Rules

- `start()` and `stop()` methods for SDK-owned runtimes and transports are idempotent.
- `stop()` methods that are declared `noexcept` will never throw.
- Destructors will never throw.

### Lock Ordering Rules

To prevent deadlocks, internal mutexes held across module boundaries must follow this ordering:

1. **Session mutex** (`Session::mutex_`)
2. **Router outbound mutex** (`Router::mutex_`)
3. **Router inbound state mutex** (`InboundState::mutex`)
4. **Transport-specific mutexes** (implementation-defined)
5. **Client mutex** (`Client::mutex_`)

When multiple mutexes must be acquired:
- Always acquire in the order listed above
- Never hold a mutex while invoking user-provided callbacks (unless explicitly documented)
- Always use `std::scoped_lock` or `std::unique_lock` for RAII-based locking

## Covered Types

### 1. `mcp::Client`

**Classification:** Thread-safe

The `Client` class provides thread-safe access to all its public methods. Internal synchronization is provided via `mutex_`.

#### Thread-Safe Methods (concurrent invocation allowed):

- `Client::create()`
- `Client::~Client()`
- `Client::session()`
- `Client::setInitializeConfiguration()`
- `Client::initializeConfiguration()`
- `Client::initialize()`
- `Client::listTools()`
- `Client::callTool()`
- `Client::listResources()`
- `Client::readResource()`
- `Client::listResourceTemplates()`
- `Client::listPrompts()`
- `Client::getPrompt()`
- `Client::setRootsProvider()`
- `Client::clearRootsProvider()`
- `Client::notifyRootsListChanged()`
- `Client::setSamplingCreateMessageHandler()`
- `Client::clearSamplingCreateMessageHandler()`
- `Client::setFormElicitationHandler()`
- `Client::clearFormElicitationHandler()`
- `Client::setUrlElicitationHandler()`
- `Client::clearUrlElicitationHandler()`
- `Client::setUrlElicitationCompletionHandler()`
- `Client::clearUrlElicitationCompletionHandler()`
- `Client::sendRequest()`
- `Client::sendRequestAsync()`
- `Client::sendNotification()`
- `Client::registerRequestHandler()`
- `Client::registerNotificationHandler()`
- `Client::negotiatedProtocolVersion()`
- `Client::negotiatedParameters()`
- `Client::negotiatedClientCapabilities()`
- `Client::negotiatedServerCapabilities()`
- `Client::supportedProtocolVersions()`

#### Lifecycle Methods (thread-safe):

- `Client::attachTransport()` - Thread-safe, but must not be called after `start()`
- `Client::connectStdio()` - Thread-safe, but must not be called after `start()`
- `Client::connectHttp()` - Thread-safe, but must not be called after `start()`
- `Client::start()` - Thread-safe, NOT idempotent (delegates to `Session::start()`, throws `LifecycleError` if state != kCreated)
- `Client::stop()` - Thread-safe, idempotent

#### Concurrency Rules:

1. Transport attachment methods (`attachTransport`, `connectStdio`, `connectHttp`) must be called before `start()` or while holding the same external synchronization as `start()`.
2. Handler registration methods may be called at any time, but handlers set after `start()` may miss early messages.
3. Configuration methods may be called at any time.

#### Callback Threading Rules:

**Current Implementation Behavior** (Note: `SessionOptions::threading` is not currently utilized):

- **Handler callbacks** (`RootsProvider`, `SamplingCreateMessageHandler`, `FormElicitationHandler`, `UrlElicitationHandler`, `UrlElicitationCompletionHandler`): Invoked directly on the router thread (I/O thread). These callbacks must be fast and non-blocking to avoid stalling message processing.
- **Async response callbacks** (`ResponseCallback` used with `sendRequestAsync`): Dispatched to an internal `boost::asio::thread_pool` (single-threaded) to avoid blocking the I/O thread.

Callback types and their actual threading:
- `RootsProvider` - Serial invocation, router/I/O thread
- `SamplingCreateMessageHandler` - Serial invocation, router/I/O thread
- `FormElicitationHandler` - Serial invocation, router/I/O thread
- `UrlElicitationHandler` - Serial invocation, router/I/O thread
- `UrlElicitationCompletionHandler` - Serial invocation, router/I/O thread
- `ResponseCallback` (async) - Serial invocation, internal callback dispatch thread pool

### 2. `mcp::Session`

**Classification:** Thread-safe

The `Session` class provides thread-safe access to all its public methods. Internal synchronization is provided via `mutex_`.

#### Thread-Safe Methods (concurrent invocation allowed):

- `Session::Session()`
- `Session::registerRequestHandler()`
- `Session::registerNotificationHandler()`
- `Session::enforceOutboundRequestLifecycle()`
- `Session::sendRequest()`
- `Session::sendRequestAsync()`
- `Session::sendNotification()`
- `Session::attachTransport()`
- `Session::state()`
- `Session::negotiatedProtocolVersion()`
- `Session::supportedProtocolVersions()`
- `Session::negotiatedParameters()` - **NOT thread-safe for concurrent mutation**. Returns a reference to internal state; the reference becomes invalid if the session is modified concurrently. External synchronization required if used concurrently with operations that may mutate session state.
- `Session::setRole()`
- `Session::role()`
- `Session::handleInitializeRequest()`
- `Session::handleInitializeResponse()`
- `Session::handleInitializedNotification()`
- `Session::configureServerInitialization()`
- `Session::canHandleRequest()`
- `Session::canSendRequest()`
- `Session::canSendNotification()`
- `Session::checkCapability()`

#### Lifecycle Methods (thread-safe):

- `Session::start()` - Thread-safe. **Not idempotent** - throws `LifecycleError` if called when state is not `kCreated`
- `Session::stop()` - Thread-safe, idempotent

#### Concurrency Rules:

1. `attachTransport()` must be called before `start()` or while holding external synchronization.
2. Handler registration may be called at any time, but handlers set after the session enters `kOperating` state may miss early messages.
3. State transitions are atomic and thread-safe.

#### Session State Threading:

The `SessionState` enum tracks the lifecycle state:
- `kCreated` -> `kInitializing` -> `kInitialized` -> `kOperating`
- -> `kStopping` -> `kStopped`

State transitions are performed atomically under `mutex_`. All state queries are atomic.

### 3. `mcp::jsonrpc::Router`

**Classification:** Thread-safe

The `Router` class provides thread-safe access to all its public methods. Internal synchronization uses separate mutexes for inbound and outbound state.

#### Thread-Safe Methods (concurrent invocation allowed):

- `Router::Router()`
- `Router::~Router()`
- `Router::setOutboundMessageSender()`
- `Router::registerRequestHandler()`
- `Router::registerNotificationHandler()`
- `Router::unregisterHandler()`
- `Router::dispatchRequest()`
- `Router::dispatchNotification()`
- `Router::sendRequest()`
- `Router::sendNotification()`
- `Router::dispatchResponse()`
- `Router::emitProgress()` (both overloads)

#### Concurrency Rules:

1. Handler registration methods may be called at any time, but handlers set after routing begins may miss early messages.
2. `setOutboundMessageSender()` must be called before dispatching messages.
3. Progress callbacks are invoked directly on the router/I/O thread.

#### Internal Lock Ordering:

The Router maintains two separate mutex domains:

1. **Outbound mutex** (`mutex_`): Protects outbound request state, handler maps, and outbound message sender
2. **Inbound state mutex** (`inboundState_->mutex`): Protects inbound request state, response promises, and completion pool

Lock ordering when both are needed: acquire outbound mutex first, then inbound state mutex.

### 4. `mcp::transport::Transport`

**Classification:** Thread-compatible (implementations may provide stronger guarantees)

The `Transport` base class defines the interface. Concrete implementations determine their own thread-safety classification.

#### Interface Methods:

- `Transport::attach()` - Called during setup phase, before `start()`
- `Transport::start()` - Called once during lifecycle
- `Transport::stop()` - Called during teardown
- `Transport::isRunning()` - Query method
- `Transport::send()` - Called during operation

#### Concurrency Rules:

1. `attach()` must complete before `start()` is called.
2. `start()` must complete before `send()` is called.
3. `isRunning()` may be called at any time.
4. `stop()` must be called after `start()` and before destruction.

### 5. `mcp::transport::HttpServerRuntime`

**Classification:** Thread-safe

The HTTP server runtime provides internal synchronization for all public methods.

#### Thread-Safe Methods (concurrent invocation allowed):

- `HttpServerRuntime::HttpServerRuntime()`
- `HttpServerRuntime::~HttpServerRuntime()`
- `HttpServerRuntime::setRequestHandler()` - Thread-safe
- `HttpServerRuntime::start()` - Thread-safe, idempotent
- `HttpServerRuntime::stop()` - Thread-safe, idempotent, noexcept
- `HttpServerRuntime::isRunning()` - Thread-safe
- `HttpServerRuntime::localPort()` - Thread-safe

#### Concurrency Rules:

1. `setRequestHandler()` may be called before `start()` or at any time during operation.
2. `start()` is idempotent; multiple calls have no effect after the first successful call.
3. `stop()` is idempotent and noexcept; it never throws.
4. Handler callbacks are invoked on the HTTP server's internal I/O threads.

### 6. `mcp::transport::HttpClientRuntime`

**Classification:** Thread-compatible

The HTTP client runtime is designed for single-threaded or externally synchronized use.

#### Thread-Compatible Methods:

- `HttpClientRuntime::HttpClientRuntime()`
- `HttpClientRuntime::~HttpClientRuntime()`
- `HttpClientRuntime::execute()` - Must not be called concurrently

#### Concurrency Rules:

1. The `execute()` method must not be called concurrently from multiple threads.
2. External synchronization is required for concurrent use.

### 7. `mcp::transport::http::StreamableHttpServer`

**Classification:** Thread-safe

The streamable HTTP server provides internal synchronization for session management and request handling.

#### Thread-Safe Methods (concurrent invocation allowed):

- `StreamableHttpServer::StreamableHttpServer()`
- `StreamableHttpServer::~StreamableHttpServer()`
- `StreamableHttpServer::setRequestHandler()`
- `StreamableHttpServer::setNotificationHandler()`
- `StreamableHttpServer::setResponseHandler()`
- `StreamableHttpServer::upsertSession()`
- `StreamableHttpServer::setSessionState()`
- `StreamableHttpServer::handleRequest()` - Thread-safe
- `StreamableHttpServer::enqueueServerMessage()` - Thread-safe

#### Concurrency Rules:

1. Handler registration methods may be called at any time.
2. `handleRequest()` is thread-safe and may be called concurrently from multiple HTTP worker threads.
3. Session management methods (`upsertSession`, `setSessionState`) are thread-safe.
4. Handler callbacks are invoked on the HTTP server's internal I/O threads.

### 8. `mcp::transport::http::StreamableHttpClient`

**Classification:** Thread-compatible

The streamable HTTP client is designed for single-threaded or externally synchronized use.

#### Thread-Compatible Methods:

- `StreamableHttpClient::StreamableHttpClient()`
- `StreamableHttpClient::~StreamableHttpClient()`
- `StreamableHttpClient::send()` - Must not be called concurrently
- `StreamableHttpClient::openListenStream()` - Must not be called concurrently
- `StreamableHttpClient::pollListenStream()` - Must not be called concurrently
- `StreamableHttpClient::hasActiveListenStream()` - Thread-safe
- `StreamableHttpClient::terminateSession()` - Must not be called concurrently

#### Concurrency Rules:

1. All mutating methods (`send`, `openListenStream`, `pollListenStream`, `terminateSession`) must not be called concurrently.
2. External synchronization is required for concurrent use.
3. `hasActiveListenStream()` is thread-safe for queries.

### 9. `mcp::StdioServerRunner`

**Classification:** Thread-safe

The STDIO server runner provides internal synchronization for lifecycle management.

#### Thread-Safe Methods (concurrent invocation allowed):

- `StdioServerRunner::StdioServerRunner()`
- `StdioServerRunner::~StdioServerRunner()`
- `StdioServerRunner::run()` - Blocking, must not be called concurrently
- `StdioServerRunner::run(input, output, errorStream)` - Blocking, must not be called concurrently
- `StdioServerRunner::startAsync()` - Thread-safe, idempotent
- `StdioServerRunner::stop()` - Thread-safe, idempotent
- `StdioServerRunner::options()` - Thread-safe

#### Concurrency Rules:

1. Only one of `run()` or `startAsync()` may be called over the lifetime of a runner instance.
2. `stop()` may be called from any thread to signal termination.
3. `stop()` sets an atomic flag; the caller must close the input stream to unblock blocking reads.
4. When using `startAsync()`, the returned thread must be joined by the caller.

### 10. `mcp::StreamableHttpServerRunner`

**Classification:** Thread-safe

The Streamable HTTP server runner provides internal synchronization for lifecycle management.

#### Thread-Safe Methods (concurrent invocation allowed):

- `StreamableHttpServerRunner::StreamableHttpServerRunner()`
- `StreamableHttpServerRunner::~StreamableHttpServerRunner()`
- `StreamableHttpServerRunner::start()` - Thread-safe, idempotent
- `StreamableHttpServerRunner::stop()` - Thread-safe, idempotent
- `StreamableHttpServerRunner::isRunning()` - Thread-safe
- `StreamableHttpServerRunner::localPort()` - Thread-safe
- `StreamableHttpServerRunner::options()` - Thread-safe

#### Concurrency Rules:

1. `start()` is idempotent; multiple calls have no effect after the first successful call.
2. `stop()` is idempotent and blocks until all connections are gracefully closed.
3. `isRunning()` and `localPort()` may be called from any thread.

### 11. `mcp::CombinedServerRunner`

**Classification:** Thread-safe

The combined server runner provides internal synchronization for lifecycle management of multiple transports.

#### Thread-Safe Methods (concurrent invocation allowed):

- `CombinedServerRunner::CombinedServerRunner()`
- `CombinedServerRunner::~CombinedServerRunner()`
- `CombinedServerRunner::start()` - Thread-safe, idempotent
- `CombinedServerRunner::stop()` - Thread-safe, idempotent
- `CombinedServerRunner::runStdio()` - Blocking if STDIO enabled
- `CombinedServerRunner::startHttp()` - Thread-safe, idempotent
- `CombinedServerRunner::stopHttp()` - Thread-safe, idempotent
- `CombinedServerRunner::isHttpRunning()` - Thread-safe
- `CombinedServerRunner::localPort()` - Thread-safe
- `CombinedServerRunner::options()` - Thread-safe
- `CombinedServerRunner::stdioRunner()` - Thread-safe
- `CombinedServerRunner::httpRunner()` - Thread-safe

#### Concurrency Rules:

1. `start()` starts all enabled transports. STDIO runs in the current thread while HTTP runs in the background.
2. `stop()` stops all enabled transports. This does not affect STDIO (which exits when stdin closes).
3. Individual transport methods (`runStdio`, `startHttp`, `stopHttp`) may be called independently.
4. When both transports are enabled, STDIO runs blocking while HTTP is non-blocking.

### 12. `mcp::transport::StdioTransport`

**Classification:** Thread-compatible (instance methods deprecated); static methods are thread-safe

The `StdioTransport` class provides both deprecated instance-based methods (which throw) and static utility methods for STDIO transport operations.

#### Static Thread-Safe Methods:

- `StdioTransport::run()` - Blocking, runs until EOF; thread-safe but only one invocation should be active per process for the same streams
- `StdioTransport::attach()` - Blocking, runs until EOF; thread-safe
- `StdioTransport::routeIncomingLine()` - Thread-safe
- `StdioTransport::spawnSubprocess()` - Thread-safe

#### Deprecated Instance Methods (throw when called):

- `StdioTransport::StdioTransport()` - Deprecated, throws
- `StdioTransport::attach()` - Deprecated, throws
- `StdioTransport::start()` - Deprecated, throws
- `StdioTransport::stop()` - Deprecated, throws
- `StdioTransport::isRunning()` - Deprecated
- `StdioTransport::send()` - Deprecated, throws

#### Concurrency Rules:

1. For static `StdioTransport::run()`, only one invocation should be active per process for the same streams.
2. The deprecated instance methods are non-functional and will throw when called.

### 13. `mcp::transport::StdioSubprocess`

**Classification:** Thread-compatible

The `StdioSubprocess` class manages a spawned subprocess with stdin/stdout communication. Designed for single-threaded or externally synchronized use.

#### Thread-Compatible Methods (external synchronization required for concurrent mutation):

- `StdioSubprocess::writeLine()` - Must not be called concurrently
- `StdioSubprocess::readLine()` - Must not be called concurrently
- `StdioSubprocess::closeStdin()` - Must not be called concurrently
- `StdioSubprocess::shutdown()` - Must not be called concurrently

#### Thread-Safe Query Methods:

- `StdioSubprocess::valid()` - Thread-safe
- `StdioSubprocess::isRunning()` - Thread-safe
- `StdioSubprocess::exitCode()` - Thread-safe
- `StdioSubprocess::capturedStderr()` - Thread-safe
- `StdioSubprocess::waitForExit()` - Thread-safe

#### Concurrency Rules:

1. Methods that modify state (`writeLine()`, `readLine()`, `closeStdin()`, `shutdown()`) must not be called concurrently.
2. External synchronization is required if the instance is accessed from multiple threads.
3. Query methods may be called concurrently with each other but not during mutation.

### 14. `mcp::transport::http::SharedHeaderState`

**Classification:** Thread-safe

The `SharedHeaderState` class provides thread-safe access to HTTP header state that may be shared between multiple components (e.g., HTTP client and its header state).

#### Thread-Safe Methods (concurrent invocation allowed):

- `SharedHeaderState::captureFromInitializeResponse()` - Thread-safe
- `SharedHeaderState::clear()` - Thread-safe
- `SharedHeaderState::sessionId()` - Thread-safe
- `SharedHeaderState::replayOnSubsequentRequests()` - Thread-safe
- `SharedHeaderState::negotiatedProtocolVersion()` - Thread-safe
- `SharedHeaderState::replayToRequestHeaders()` - Thread-safe

#### Concurrency Rules:

1. All methods provide internal synchronization via `mutex_`.
2. Safe to share between threads and components.
3. Header replay operations are atomic under the internal lock.

## Callback Invocation Threading Summary

| Callback Type | Threading | Invocation Pattern | Notes |
|---------------|-----------|-------------------|-------|
| `jsonrpc::RequestHandler` | Router/I/O thread | Serial per request | Invoked directly on router thread |
| `jsonrpc::NotificationHandler` | Router/I/O thread | Serial per notification | Invoked directly on router thread |
| `jsonrpc::OutboundMessageSender` | Caller thread | Serial per message | Threading determined by caller context |
| `jsonrpc::ProgressCallback` | Router/I/O thread | Serial per progress token | Invoked during request processing |
| `ResponseCallback` | Callback dispatch thread pool | Serial per response | Posted to internal thread pool via `sendRequestAsync` |
| `RootsProvider` | Router/I/O thread | Serial | Client-side handler, invoked directly |
| `SamplingCreateMessageHandler` | Router/I/O thread | Serial | Client-side handler, invoked directly |
| `FormElicitationHandler` | Router/I/O thread | Serial | Client-side handler, invoked directly |
| `UrlElicitationHandler` | Router/I/O thread | Serial | Client-side handler, invoked directly |
| `UrlElicitationCompletionHandler` | Router/I/O thread | Serial | Client-side handler, invoked directly |
| `http::StreamableRequestHandler` | HTTP server I/O thread | Serial per request | HTTP server handler |
| `http::StreamableNotificationHandler` | HTTP server I/O thread | Serial per notification | HTTP server handler |
| `http::StreamableResponseHandler` | HTTP server I/O thread | Serial per response | HTTP server handler |
| `http::HttpRequestHandler` | HTTP server I/O thread | Serial per request | HTTP runtime handler |
| `ServerFactory` | Caller thread | Per-invocation | Creates Server instances |

**Note**: The `SessionOptions::threading` configuration field is defined but not currently utilized by the runtime implementation. The threading behavior documented above reflects the actual implementation.

## HandlerThreadingPolicy Configuration

The `HandlerThreadingPolicy` enum is defined in the API for future use:

```cpp
enum class HandlerThreadingPolicy : std::uint8_t
{
  kIoThread,   // Callbacks are invoked on the transport I/O thread
  kExecutor,   // Callbacks are dispatched to the configured Executor
};
```

**Note**: This enum is part of the `SessionOptions::threading` configuration structure, but the runtime implementation does not currently utilize this policy. Callback threading behavior is fixed as documented in the Callback Threading Rules sections above.

### Future Behavior (when implemented)

- **kIoThread**: Callbacks would be invoked directly on the transport I/O thread
- **kExecutor**: Callbacks would be dispatched to a configured `Executor`

## Lifecycle Rules Summary

### Start/Stop Idempotency

Lifecycle method idempotency varies by type:

- **Idempotent `start()`**: `HttpServerRuntime::start()`, `StreamableHttpServerRunner::start()`, `CombinedServerRunner::start()`, `StdioServerRunner::startAsync()`
  - Multiple calls have the same effect as a single call
  
- **Non-idempotent `start()`**: `Session::start()`, `Client::start()`
  - Throws `LifecycleError` if called when state is not `kCreated`
  - Must only be called once per session instance

- **Idempotent `stop()`**: All `stop()` methods are idempotent
  - Calling `stop()` when not started is a no-op

### Destructor Guarantees

All SDK destructors provide the following guarantees:

- **Never throw**: Destructors are marked `noexcept` and will never throw exceptions
- **Safe to destroy from any thread**: Destruction is thread-safe
- **Automatic cleanup**: Destructors automatically stop and clean up resources

### Stop Guarantees

`stop()` methods that are declared `noexcept` provide these guarantees:

- **Never throw**: `noexcept` stop methods will never throw
- **Graceful shutdown**: Stop methods block until graceful shutdown completes (unless specified otherwise)
- **Resource cleanup**: All resources are properly released

## Lock Ordering Reference

When acquiring multiple locks in the SDK, always follow this order:

1. `Session::mutex_`
2. `Router::mutex_` (outbound)
3. `InboundState::mutex` (inbound)
4. Transport implementation mutexes
5. `Client::mutex_`

Example:
```cpp
// Correct ordering
std::scoped_lock sessionLock(session.mutex_);
std::scoped_lock routerLock(router.mutex_);
// ... access protected data ...
```

Never acquire locks in reverse order, and never hold a lock while invoking user callbacks (unless explicitly documented as an exception).

## Thread-Compatible Types

The following types are classified as Thread-compatible:

- `mcp::transport::HttpClientRuntime`
- `mcp::transport::http::StreamableHttpClient`

Users must provide external synchronization when using these types from multiple threads concurrently.

## Thread-Confined Types

No types in the current SDK API are classified as Thread-confined.

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-02-21 | Initial thread-safety contract definition |
