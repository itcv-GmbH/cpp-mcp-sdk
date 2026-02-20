#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <mcp/server/server.hpp>
#include <mcp/transport/http.hpp>

namespace mcp
{

/// @brief ServerFactory is a session-agnostic factory function that creates new Server instances.
/// @details Each invocation creates a fresh Server instance with its own Session. This is used by
/// runners that need to create per-session Server instances (e.g., Streamable HTTP server with
/// requireSessionId=true).
using ServerFactory = std::function<std::shared_ptr<Server>()>;

/// Configuration options for the Streamable HTTP server runner.
struct StreamableHttpServerRunnerOptions
{
  /// Options passed through to the underlying Streamable HTTP transport.
  /// Controls endpoint configuration, TLS, authorization, etc.
  transport::http::StreamableHttpServerOptions transportOptions;
};

/// Runner for serving MCP over Streamable HTTP.
///
/// This runner provides a start/stop API for running an MCP server
/// over HTTP with SSE streaming support. It owns the HTTP server runtime
/// and uses a ServerFactory to create Server instances per session.
///
/// For multi-client safety, use `transportOptions.http.requireSessionId = true`.
/// This ensures each client gets a unique session ID, enabling proper request
/// routing and session isolation. See task-002 for details on the session
/// isolation contract.
///
/// Usage:
/// @code
///   ServerFactory makeServer = [] { return mcp::Server::create(); };
///   mcp::StreamableHttpServerRunner runner(makeServer);
///   runner.start();
///   std::cout << "Server running on port " << runner.localPort() << std::endl;
///   // ... server is handling requests ...
///   runner.stop();
/// @endcode
///
/// Or with custom options:
/// @code
///   StreamableHttpServerRunnerOptions options;
///   options.transportOptions.http.endpoint.port = 8080;
///   options.transportOptions.http.endpoint.path = "/mcp";
///   options.transportOptions.http.requireSessionId = true;  // Multi-client safe
///   ServerFactory makeServer = [] { return mcp::Server::create(); };
///   mcp::StreamableHttpServerRunner runner(makeServer, options);
///   runner.start();
/// @endcode
class StreamableHttpServerRunner final
{
public:
  /// Constructs a runner with a ServerFactory and default options.
  explicit StreamableHttpServerRunner(ServerFactory serverFactory);

  /// Constructs a runner with a ServerFactory and custom options.
  StreamableHttpServerRunner(ServerFactory serverFactory, StreamableHttpServerRunnerOptions options);

  ~StreamableHttpServerRunner();

  StreamableHttpServerRunner(const StreamableHttpServerRunner &) = delete;
  auto operator=(const StreamableHttpServerRunner &) -> StreamableHttpServerRunner & = delete;
  StreamableHttpServerRunner(StreamableHttpServerRunner &&other) noexcept;
  auto operator=(StreamableHttpServerRunner &&other) noexcept -> StreamableHttpServerRunner &;

  /// Starts the HTTP server and begins accepting connections.
  ///
  /// After calling start(), the server will accept HTTP requests on the
  /// configured endpoint. Use localPort() to determine which port was
  /// assigned (useful when port is set to 0 for dynamic allocation).
  ///
  /// @throws std::runtime_error if the server fails to start.
  auto start() -> void;

  /// Stops the HTTP server and closes all active connections.
  ///
  /// This method blocks until all connections are gracefully closed.
  auto stop() -> void;

  /// Returns whether the server is currently running.
  ///
  /// @returns true if start() has been called and stop() has not yet been called.
  [[nodiscard]] auto isRunning() const noexcept -> bool;

  /// Returns the local port the server is listening on.
  ///
  /// This is useful when the port was set to 0 (dynamic allocation) in
  /// the options, as the actual port is assigned by the operating system.
  ///
  /// @returns The port number, or 0 if the server is not running.
  [[nodiscard]] auto localPort() const noexcept -> std::uint16_t;

  /// Returns the options used by this runner.
  [[nodiscard]] auto options() const -> const StreamableHttpServerRunnerOptions &;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// @brief Runner behavior rules for different transport configurations.
///
/// ## STDIO Runner
/// - Uses exactly one Server instance for its entire lifetime.
/// - Calls Server::start() once during runner initialization.
/// - Calls Server::stop() when the runner is stopped or destroyed.
///
/// ## Streamable HTTP Runner
/// ### requireSessionId=true (multi-client mode)
/// - Creates a new Server instance per unique MCP-Session-Id.
/// - Session creation is triggered on the first accepted "initialize" request for a newly issued
///   MCP-Session-Id.
/// - Uses RequestContext.sessionId as the key to track per-session servers.
/// - Treats missing sessionId in requests as an internal error.
/// - Each per-session Server instance must call Server::start() before handling any messages.
/// - Each per-session Server instance must call Server::stop() before being dropped.
///
/// ### requireSessionId=false (single-client mode)
/// - Uses exactly one Server instance for all requests.
/// - Treats RequestContext.sessionId as std::nullopt.
/// - Calls Server::start() once during runner initialization.
/// - Calls Server::stop() when the runner is stopped or destroyed.
///
/// ## Combined Runner
/// - Creates one Server instance for STDIO transport.
/// - Creates one Server instance per HTTP session when requireSessionId=true.
/// - Uses the same lifecycle rules as individual runners for each transport.
///
/// @note Session isolation is critical because mcp::Session lifecycle state (e.g., protocol version
/// negotiation, capability initialization) is per-instance. Routing multiple sessions through a
/// single Server instance would cause initialization/lifecycle state to be shared incorrectly.
namespace runner
{

/// @brief Session isolation rules for Streamable HTTP runners.
namespace session_rules
{

/// @brief Defines when a new HTTP session server is created.
/// @details A new Server instance is created on the first accepted "initialize" request for a
/// newly issued MCP-Session-Id. This ensures each session has its own initialized state.
inline constexpr std::string_view kSessionCreationTrigger = "on first accepted 'initialize' request for a newly issued MCP-Session-Id";

/// @brief Cleanup trigger: HTTP DELETE
/// @details When an HTTP DELETE request is received for a specific sessionId, the runner must
/// drop the corresponding Server instance after calling Server::stop().
inline constexpr std::string_view kCleanupTriggerHttpDelete = "drop per-session servers on HTTP DELETE for the sessionId";

/// @brief Cleanup trigger: HTTP 404
/// @details When the transport returns HTTP 404 for a session (indicating the session is expired
/// or terminated), the runner must drop the corresponding Server instance after calling
/// Server::stop().
inline constexpr std::string_view kCleanupTriggerHttp404 = "drop per-session servers when the transport returns HTTP 404 for that session (expired/terminated)";

/// @brief Session-keying behavior for requireSessionId=true
/// @details When StreamableHttpServerOptions.http.requireSessionId is true:
/// - Use RequestContext.sessionId as the key to track and route requests to the correct
///   per-session Server instance.
/// - Treat missing sessionId in requests as an internal error.
inline constexpr std::string_view kSessionKeyingRequireSessionIdTrue = "use RequestContext.sessionId as the key; treat missing sessionId as an internal error";

/// @brief Session-keying behavior for requireSessionId=false
/// @details When StreamableHttpServerOptions.http.requireSessionId is false:
/// - Use a single fixed key for all requests (all requests route to the same Server instance).
/// - Treat RequestContext.sessionId as std::nullopt (the header is ignored).
inline constexpr std::string_view kSessionKeyingRequireSessionIdFalse = "use a single fixed key for all requests; treat RequestContext.sessionId as std::nullopt";

}  // namespace session_rules

/// @brief Runner lifecycle rules for Server instances.
namespace lifecycle_rules
{

/// @brief Start requirement
/// @details The runner must call mcp::Server::start() for every Server instance it creates
/// before handling any messages. This ensures the session is properly initialized and ready to
/// process requests.
inline constexpr std::string_view kStartRequirement = "runner must call mcp::Server::start() for every server instance it creates before handling any messages";

/// @brief Stop triggers
/// @details The runner must call mcp::Server::stop() before dropping a Server instance due to:
/// - HTTP DELETE for the session
/// - HTTP 404 cleanup (session expired/terminated)
/// - Runner stop
/// - Runner destruction
inline constexpr std::string_view kStopTriggers =
  "runner must call mcp::Server::stop() before dropping a server instance due to HTTP DELETE, "
  "HTTP 404 cleanup, runner stop, or runner destruction";

}  // namespace lifecycle_rules

}  // namespace runner

}  // namespace mcp
