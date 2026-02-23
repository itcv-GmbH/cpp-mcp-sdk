#pragma once

/**
 * @file session.hpp
 * @brief Canonical header for Session class and session lifecycle components.
 *
 * This header provides the Session class declaration in namespace mcp::lifecycle,
 * along with all session-related types.
 * For finer-grained control, include individual headers from mcp/lifecycle/session/.
 */

#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <mcp/jsonrpc/router.hpp>
#include <mcp/lifecycle/session/capability_error.hpp>
#include <mcp/lifecycle/session/client_capabilities.hpp>
#include <mcp/lifecycle/session/lifecycle_error.hpp>
#include <mcp/lifecycle/session/negotiated_parameters.hpp>
#include <mcp/lifecycle/session/request_options.hpp>
#include <mcp/lifecycle/session/response_callback.hpp>
#include <mcp/lifecycle/session/server_capabilities.hpp>
#include <mcp/lifecycle/session/session_options.hpp>
#include <mcp/lifecycle/session/session_role.hpp>
#include <mcp/lifecycle/session/session_state.hpp>

namespace mcp
{
namespace transport
{
class Transport;
}  // namespace transport

namespace lifecycle
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
 *   (request handler exceptions are caught and converted to JSON-RPC error responses by Router;
 *   notification handler exceptions propagate to the caller since notifications have no response)
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
 * - handleInitializedNotification() does not throw
 * - configureServerInitialization() does not throw
 */
class Session : public std::enable_shared_from_this<Session>
{
public:
  explicit Session(session::SessionOptions options = {});

  // Handler registration
  auto registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void;
  auto registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void;

  // Message sending
  auto enforceOutboundRequestLifecycle(std::string_view method, jsonrpc::JsonValue params, session::RequestOptions options = {}) -> void;
  // Compatibility wrapper: enforces lifecycle and returns a placeholder response future.
  auto sendRequest(const std::string &method, jsonrpc::JsonValue params, session::RequestOptions options = {}) -> std::future<jsonrpc::Response>;
  auto sendRequestAsync(const std::string &method, jsonrpc::JsonValue params, const session::ResponseCallback &callback, session::RequestOptions options = {}) -> void;
  auto sendNotification(std::string method, std::optional<jsonrpc::JsonValue> params = std::nullopt) -> void;

  // Transport and lifecycle
  auto attachTransport(std::shared_ptr<transport::Transport> transport) -> void;
  auto start() -> void;
  auto stop() noexcept -> void;

  // State accessors
  auto state() const noexcept -> session::SessionState;
  auto negotiatedProtocolVersion() const noexcept -> std::optional<std::string_view>;
  auto supportedProtocolVersions() const -> const std::vector<std::string> &;
  auto negotiatedParameters() const -> const std::optional<session::NegotiatedParameters> &;

  // Role management
  auto setRole(session::SessionRole role) -> void;
  auto role() const noexcept -> session::SessionRole;

  // Initialize handling
  auto handleInitializeRequest(const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleInitializeResponse(const jsonrpc::Response &response) -> void;
  auto handleInitializedNotification() -> void;
  auto configureServerInitialization(session::ServerCapabilities capabilities, session::Implementation serverInfo, std::optional<std::string> instructions = std::nullopt) -> void;

  // Lifecycle enforcement
  auto canHandleRequest(std::string_view method) const -> bool;
  auto canSendRequest(std::string_view method) const -> bool;
  auto canSendNotification(std::string_view method) const -> bool;

  // Capability checking
  auto checkCapability(std::string_view capability) const -> bool;

private:
  session::SessionOptions options_;
  jsonrpc::Router router_;
  std::shared_ptr<transport::Transport> transport_;
  session::SessionState state_ = session::SessionState::kCreated;
  session::SessionRole role_ = session::SessionRole::kClient;
  std::optional<session::NegotiatedParameters> negotiatedParams_;
  std::optional<session::ClientCapabilities> pendingClientCapabilities_;
  std::optional<session::Implementation> pendingClientInfo_;
  session::ServerCapabilities configuredServerCapabilities_;
  session::Implementation configuredServerInfo_;
  std::optional<std::string> configuredServerInstructions_;
  mutable std::mutex mutex_;
};

}  // namespace lifecycle
}  // namespace mcp
