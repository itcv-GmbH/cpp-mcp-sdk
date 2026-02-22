#pragma once

#include <cstdint>
#include <memory>

#include <mcp/server/detail/server_factory.hpp>
#include <mcp/server/detail/streamable_http_server_runner_options.hpp>

namespace mcp
{

/// @section Exceptions
///
/// The StreamableHttpServerRunner provides the following exception guarantees:
/// - Constructor: Does not throw
/// - Destructor: noexcept (guaranteed not to throw; calls stop() internally)
/// - Move operations: noexcept
/// - start(): Idempotent; may throw std::runtime_error on server startup failure
/// - stop() noexcept: Idempotent; never throws; blocks until server stops
/// - isRunning() noexcept: Safe to call from any thread
/// - localPort() noexcept: Safe to call from any thread
/// - options() noexcept: Safe to call from any thread
///
/// @par Threading Guarantees
/// - start() is thread-safe and idempotent
/// - stop() is thread-safe, idempotent, and noexcept
/// - stop() joins the server thread deterministically without deadlock
/// - The background thread has a noexcept entrypoint
/// - All exceptions in the background thread are caught and reported via ErrorReporter
///
/// @par Lifecycle
/// 1. Create the runner with a ServerFactory and options
/// 2. Call start() to begin accepting connections (idempotent)
/// 3. Use isRunning() and localPort() to check status
/// 4. Call stop() to shut down (idempotent, noexcept)
/// 5. Destroy the runner (destructor is noexcept)

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
  auto stop() noexcept -> void;

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

}  // namespace mcp
