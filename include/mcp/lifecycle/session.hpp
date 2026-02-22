#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <jsoncons/json.hpp>
#include <mcp/error_reporter.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/version.hpp>

namespace mcp
{
/**
 * @brief Session lifecycle management.
 *
 * @section Thread Safety
 *
 * @par Thread-Safety Classification: Thread-safe
 *
 * The Session class provides thread-safe access to all its public methods.
 * Internal synchronization is provided via mutex_.
 *
 * @par Thread-Safe Methods (concurrent invocation allowed):
 * - Session()
 * - registerRequestHandler(), registerNotificationHandler()
 * - enforceOutboundRequestLifecycle(), sendRequest(), sendRequestAsync(), sendNotification()
 * - attachTransport()
 * - state(), negotiatedProtocolVersion(), supportedProtocolVersions()
 * - negotiatedParameters() - Returns reference to internal state; NOT thread-safe for
 *   concurrent mutation. External synchronization required if used concurrently with
 *   operations that may mutate session state.
 * - setRole(), role()
 * - handleInitializeRequest(), handleInitializeResponse(), handleInitializedNotification()
 * - configureServerInitialization()
 * - canHandleRequest(), canSendRequest(), canSendNotification()
 * - checkCapability()
 *
 * @par Lifecycle Methods (thread-safe):
 * - start() - Thread-safe. Not idempotent - throws LifecycleError if called when state
 *   is not kCreated. Must only be called once per session instance.
 * - stop() - Thread-safe, idempotent. Safe to call from any thread. Never throws.
 *
 * @par Concurrency Rules:
 * 1. attachTransport() must be called before start() or while holding external synchronization.
 * 2. Handler registration may be called at any time, but handlers set after the session
 *    enters kOperating state may miss early messages.
 * 3. State transitions are atomic and thread-safe.
 *
 * @par Session State Threading:
 * The SessionState enum tracks the lifecycle state:
 * - kCreated -> kInitializing -> kInitialized -> kOperating
 * - -> kStopping -> kStopped
 *
 * State transitions are performed atomically under mutex_. All state queries are atomic.
 *
 * @par Handler Threading Configuration:
 * The SessionOptions::threading field allows configuration of handler threading behavior:
 * - HandlerThreadingPolicy::kIoThread: Handlers are invoked directly on the I/O thread
 * - HandlerThreadingPolicy::kExecutor: Handlers are dispatched to the configured Executor
 *
 * Note: The threading policy is stored in SessionOptions but actual threading behavior
 * is determined by the Router and Client/Server implementations that use the Session.
 * Callbacks may be invoked on I/O threads or internal worker threads depending on
 * the transport and client/server configuration.
 *
 * @section Exceptions
 *
 * @subsection Exception Types
 * - LifecycleError: Thrown on invalid session state transitions or operations in wrong state
 *   Inherits from std::runtime_error
 * - CapabilityError: Thrown when a capability check fails
 *   Inherits from std::runtime_error
 *
 * @subsection Construction
 * - Session(SessionOptions) does not throw
 *
 * @subsection Destruction
 * - ~Session() uses default destructor (implicitly noexcept)
 *
 * @subsection Lifecycle Operations (throwing)
 * - attachTransport() throws std::runtime_error on transport error
 * - start() may throw std::runtime_error on startup failure (throws LifecycleError if not in kCreated state)
 * - stop() is noexcept - never throws
 *
 * @subsection Request Operations (throwing)
 * - sendRequest() throws LifecycleError if session is not in appropriate state
 * - enforceOutboundRequestLifecycle() throws LifecycleError for invalid state
 * - sendRequestAsync() - exceptions in callback are suppressed by caller
 * - sendNotification() may throw LifecycleError for invalid state
 *
 * @subsection Handler Registration
 * - registerRequestHandler(), registerNotificationHandler() do not throw
 *   (handler exceptions are caught and converted to JSON-RPC error responses by Router)
 *
 * @subsection State Accessors
 * - state() noexcept
 * - negotiatedProtocolVersion() noexcept
 * - role() noexcept
 * - supportedProtocolVersions() returns const ref (thread-safe, never modified after construction)
 * - negotiatedParameters() returns const optional ref (protected by mutex_)
 *
 * @subsection Capability Checking
 * - checkCapability() const (thread-safe)
 * - canHandleRequest(), canSendRequest(), canSendNotification() const (thread-safe)
 *
 * @subsection Initialize Handling
 * - handleInitializeRequest() may throw on protocol violation
 * - handleInitializeResponse() may throw on invalid response
 * - handleInitializedNotification() noexcept
 * - configureServerInitialization() does not throw
 */

/**
 * @brief Thread Safety
 *
 * @par Thread-Safety Classification: Thread-safe
 *
 * The Session class provides thread-safe access to all its public methods.
 * Internal synchronization is provided via mutex_.
 *
 * @par Thread-Safe Methods (concurrent invocation allowed):
 * - Session()
 * - registerRequestHandler(), registerNotificationHandler()
 * - enforceOutboundRequestLifecycle(), sendRequest(), sendRequestAsync(), sendNotification()
 * - attachTransport()
 * - state(), negotiatedProtocolVersion(), supportedProtocolVersions()
 * - negotiatedParameters() - Returns reference to internal state; NOT thread-safe for
 *   concurrent mutation. External synchronization required if used concurrently with
 *   operations that may mutate session state.
 * - setRole(), role()
 * - handleInitializeRequest(), handleInitializeResponse(), handleInitializedNotification()
 * - configureServerInitialization()
 * - canHandleRequest(), canSendRequest(), canSendNotification()
 * - checkCapability()
 *
 * @par Lifecycle Methods (thread-safe):
 * - start() - Thread-safe. Not idempotent - throws LifecycleError if called when state
 *   is not kCreated. Must only be called once per session instance.
 * - stop() - Thread-safe, idempotent
 *
 * @par Concurrency Rules:
 * 1. attachTransport() must be called before start() or while holding external synchronization.
 * 2. Handler registration may be called at any time, but handlers set after the session
 *    enters kOperating state may miss early messages.
 * 3. State transitions are atomic and thread-safe.
 *
 * @par Session State Threading:
 * The SessionState enum tracks the lifecycle state:
 * - kCreated -> kInitializing -> kInitialized -> kOperating
 * - -> kStopping -> kStopped
 *
 * State transitions are performed atomically under mutex_. All state queries are atomic.
 */

namespace transport
{
class Transport;
}  // namespace transport

class Executor
{
public:
  virtual ~Executor() = default;
  Executor() = default;
  Executor(const Executor &) = delete;
  Executor(Executor &&) = delete;
  auto operator=(const Executor &) -> Executor & = delete;
  auto operator=(Executor &&) -> Executor & = delete;
  virtual auto post(std::function<void()> task) -> void = 0;
};

enum class HandlerThreadingPolicy : std::uint8_t
{
  kIoThread,
  kExecutor,
};

struct SessionThreading
{
  HandlerThreadingPolicy handlerThreadingPolicy = HandlerThreadingPolicy::kExecutor;
  std::shared_ptr<Executor> handlerExecutor;
};

struct SessionOptions
{
  SessionThreading threading;
  std::vector<std::string> supportedProtocolVersions = {
    std::string(kLatestProtocolVersion),
    std::string(kLegacyProtocolVersion),
  };
  /// Error reporter callback for background execution context failures.
  /// If not set, errors are silently suppressed.
  ErrorReporter errorReporter;
};

struct RequestOptions
{
  std::chrono::milliseconds timeout {0};
  bool cancelOnTimeout = true;
};

using ResponseCallback = std::function<void(const jsonrpc::Response &)>;

enum class SessionState : std::uint8_t
{
  kCreated,  // Session created, not yet initialized
  kInitializing,  // Initialize request sent/received, waiting for response
  kInitialized,  // Initialize response received/sent, waiting for initialized notification
  kOperating,  // Full operation mode
  kStopping,  // Shutdown in progress
  kStopped,  // Session stopped
};

enum class SessionRole : std::uint8_t
{
  kClient,
  kServer,
};

class LifecycleError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class CapabilityError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

// Forward declarations
class Icon;
class Implementation;
class ClientCapabilities;
class ServerCapabilities;
class NegotiatedParameters;

// Icon - Represents an optionally-sized icon for UI display
class Icon
{
public:
  Icon() = default;
  explicit Icon(std::string src,
                std::optional<std::string> mimeType = std::nullopt,
                std::optional<std::vector<std::string>> sizes = std::nullopt,
                std::optional<std::string> theme = std::nullopt);

  auto src() const noexcept -> const std::string & { return src_; }
  auto mimeType() const noexcept -> const std::optional<std::string> & { return mimeType_; }
  auto sizes() const noexcept -> const std::optional<std::vector<std::string>> & { return sizes_; }
  auto theme() const noexcept -> const std::optional<std::string> & { return theme_; }

private:
  std::string src_;
  std::optional<std::string> mimeType_;
  std::optional<std::vector<std::string>> sizes_;
  std::optional<std::string> theme_;
};

// Implementation - Describes MCP implementation metadata
class Implementation
{
public:
  Implementation() = default;
  Implementation(std::string name,
                 std::string version,
                 std::optional<std::string> title = std::nullopt,
                 std::optional<std::string> description = std::nullopt,
                 std::optional<std::string> websiteUrl = std::nullopt,
                 std::optional<std::vector<Icon>> icons = std::nullopt);

  auto name() const noexcept -> const std::string & { return name_; }
  auto version() const noexcept -> const std::string & { return version_; }
  auto title() const noexcept -> const std::optional<std::string> & { return title_; }
  auto description() const noexcept -> const std::optional<std::string> & { return description_; }
  auto websiteUrl() const noexcept -> const std::optional<std::string> & { return websiteUrl_; }
  auto icons() const noexcept -> const std::optional<std::vector<Icon>> & { return icons_; }

private:
  std::string name_;
  std::string version_;
  std::optional<std::string> title_;
  std::optional<std::string> description_;
  std::optional<std::string> websiteUrl_;
  std::optional<std::vector<Icon>> icons_;
};

// Capability structures
struct RootsCapability
{
  bool listChanged = false;
};

struct SamplingCapability
{
  bool context = false;
  bool tools = false;
};

struct ElicitationCapability
{
  bool form = false;
  bool url = false;
};

struct TasksCapability
{
  bool list = false;
  bool cancel = false;
  bool samplingCreateMessage = false;
  bool elicitationCreate = false;
  bool toolsCall = false;
};

struct LoggingCapability
{
  // Empty struct - presence indicates support
};

struct CompletionsCapability
{
  // Empty struct - presence indicates support
};

struct PromptsCapability
{
  bool listChanged = false;
};

struct ResourcesCapability
{
  bool subscribe = false;
  bool listChanged = false;
};

struct ToolsCapability
{
  bool listChanged = false;
};

// ClientCapabilities - Capabilities a client may support
class ClientCapabilities
{
public:
  ClientCapabilities() = default;
  ClientCapabilities(std::optional<RootsCapability> roots,
                     std::optional<SamplingCapability> sampling,
                     std::optional<ElicitationCapability> elicitation,
                     std::optional<TasksCapability> tasks,
                     std::optional<jsoncons::json> experimental);

  auto roots() const noexcept -> const std::optional<RootsCapability> &;
  auto sampling() const noexcept -> const std::optional<SamplingCapability> &;
  auto elicitation() const noexcept -> const std::optional<ElicitationCapability> &;
  auto tasks() const noexcept -> const std::optional<TasksCapability> &;
  auto experimental() const noexcept -> const std::optional<jsoncons::json> &;

  auto hasCapability(std::string_view capability) const -> bool;

private:
  std::optional<RootsCapability> roots_;
  std::optional<SamplingCapability> sampling_;
  std::optional<ElicitationCapability> elicitation_;
  std::optional<TasksCapability> tasks_;
  std::optional<jsoncons::json> experimental_;  // Passthrough for experimental features
};

// ServerCapabilities - Capabilities a server may support
class ServerCapabilities
{
public:
  ServerCapabilities() = default;
  ServerCapabilities(std::optional<LoggingCapability> logging,
                     std::optional<CompletionsCapability> completions,
                     std::optional<PromptsCapability> prompts,
                     std::optional<ResourcesCapability> resources,
                     std::optional<ToolsCapability> tools,
                     std::optional<TasksCapability> tasks,
                     std::optional<jsoncons::json> experimental);

  auto logging() const noexcept -> const std::optional<LoggingCapability> &;
  auto completions() const noexcept -> const std::optional<CompletionsCapability> &;
  auto prompts() const noexcept -> const std::optional<PromptsCapability> &;
  auto resources() const noexcept -> const std::optional<ResourcesCapability> &;
  auto tools() const noexcept -> const std::optional<ToolsCapability> &;
  auto tasks() const noexcept -> const std::optional<TasksCapability> &;
  auto experimental() const noexcept -> const std::optional<jsoncons::json> &;

  auto hasCapability(std::string_view capability) const -> bool;

private:
  std::optional<LoggingCapability> logging_;
  std::optional<CompletionsCapability> completions_;
  std::optional<PromptsCapability> prompts_;
  std::optional<ResourcesCapability> resources_;
  std::optional<ToolsCapability> tools_;
  std::optional<TasksCapability> tasks_;
  std::optional<jsoncons::json> experimental_;  // Passthrough for experimental features
};

// NegotiatedParameters - Stores negotiated version and capabilities
class NegotiatedParameters
{
public:
  NegotiatedParameters() = default;
  NegotiatedParameters(std::string protocolVersion,
                       ClientCapabilities clientCaps,
                       ServerCapabilities serverCaps,
                       Implementation clientInfo,
                       Implementation serverInfo,
                       std::optional<std::string> instructions);

  auto protocolVersion() const noexcept -> std::string_view;
  auto clientCapabilities() const noexcept -> const ClientCapabilities &;
  auto serverCapabilities() const noexcept -> const ServerCapabilities &;
  auto clientInfo() const noexcept -> const Implementation &;
  auto serverInfo() const noexcept -> const Implementation &;
  auto instructions() const noexcept -> const std::optional<std::string> &;

private:
  std::string protocolVersion_;
  ClientCapabilities clientCapabilities_;
  ServerCapabilities serverCapabilities_;
  Implementation clientInfo_;
  Implementation serverInfo_;
  std::optional<std::string> instructions_;
};

class Session : public std::enable_shared_from_this<Session>
{
public:
  explicit Session(SessionOptions options = {});

  // Handler registration
  auto registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void;
  auto registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void;

  // Message sending
  auto enforceOutboundRequestLifecycle(std::string_view method, jsonrpc::JsonValue params, RequestOptions options = {}) -> void;
  // Compatibility wrapper: enforces lifecycle and returns a placeholder response future.
  auto sendRequest(const std::string &method, jsonrpc::JsonValue params, RequestOptions options = {}) -> std::future<jsonrpc::Response>;
  auto sendRequestAsync(const std::string &method, jsonrpc::JsonValue params, const ResponseCallback &callback, RequestOptions options = {}) -> void;
  auto sendNotification(std::string method, std::optional<jsonrpc::JsonValue> params = std::nullopt) -> void;

  // Transport and lifecycle
  auto attachTransport(std::shared_ptr<transport::Transport> transport) -> void;
  auto start() -> void;
  auto stop() noexcept -> void;

  // State accessors
  auto state() const noexcept -> SessionState;
  auto negotiatedProtocolVersion() const noexcept -> std::optional<std::string_view>;
  auto supportedProtocolVersions() const -> const std::vector<std::string> &;
  auto negotiatedParameters() const -> const std::optional<NegotiatedParameters> &;

  // Role management
  auto setRole(SessionRole role) -> void;
  auto role() const noexcept -> SessionRole;

  // Initialize handling
  auto handleInitializeRequest(const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleInitializeResponse(const jsonrpc::Response &response) -> void;
  auto handleInitializedNotification() -> void;
  auto configureServerInitialization(ServerCapabilities capabilities, Implementation serverInfo, std::optional<std::string> instructions = std::nullopt) -> void;

  // Lifecycle enforcement
  auto canHandleRequest(std::string_view method) const -> bool;
  auto canSendRequest(std::string_view method) const -> bool;
  auto canSendNotification(std::string_view method) const -> bool;

  // Capability checking
  auto checkCapability(std::string_view capability) const -> bool;

private:
  SessionOptions options_;
  jsonrpc::Router router_;
  std::shared_ptr<transport::Transport> transport_;
  SessionState state_ = SessionState::kCreated;
  SessionRole role_ = SessionRole::kClient;
  std::optional<NegotiatedParameters> negotiatedParams_;
  std::optional<ClientCapabilities> pendingClientCapabilities_;
  std::optional<Implementation> pendingClientInfo_;
  ServerCapabilities configuredServerCapabilities_;
  Implementation configuredServerInfo_;
  std::optional<std::string> configuredServerInstructions_;
  mutable std::mutex mutex_;
};

}  // namespace mcp
