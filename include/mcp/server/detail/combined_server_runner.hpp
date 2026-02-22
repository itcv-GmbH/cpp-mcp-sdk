#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <mcp/server/detail/combined_server_runner_options.hpp>
#include <mcp/server/detail/server_factory.hpp>
#include <mcp/server/detail/stdio_server_runner.hpp>
#include <mcp/server/detail/streamable_http_server_runner.hpp>

namespace mcp
{

/// Combined runner for serving MCP over multiple transports.
///
/// This runner supports running an MCP server over STDIO, Streamable HTTP,
/// or both simultaneously. It provides a unified API for managing multiple
/// transports.
///
/// The runner uses a ServerFactory to create Server instances:
/// - In STDIO-only mode, one Server instance is created via the factory.
/// - In HTTP-only mode, one Server instance is created per session (when requireSessionId=true).
/// - In dual-transport mode, STDIO uses one Server instance and HTTP uses one per session.
///
/// Usage (STDIO only):
/// @code
///   ServerFactory makeServer = [] { return mcp::Server::create(); };
///   CombinedServerRunnerOptions options;
///   options.enableStdio = true;
///   mcp::CombinedServerRunner runner(makeServer, options);
///   runner.runStdio();
/// @endcode
///
/// Usage (HTTP only):
/// @code
///   ServerFactory makeServer = [] { return mcp::Server::create(); };
///   CombinedServerRunnerOptions options;
///   options.enableHttp = true;
///   options.httpOptions.transportOptions.http.endpoint.port = 8080;
///   mcp::CombinedServerRunner runner(makeServer, options);
///   runner.startHttp();
/// @endcode
///
/// Usage (both STDIO and HTTP):
/// @code
///   ServerFactory makeServer = [] { return mcp::Server::create(); };
///   CombinedServerRunnerOptions options;
///   options.enableStdio = true;
///   options.enableHttp = true;
///   options.httpOptions.transportOptions.http.requireSessionId = true;
///   mcp::CombinedServerRunner runner(makeServer, options);
///   runner.start();  // Starts both transports
///   // ... server is handling requests ...
///   runner.stop();  // Stops both transports
/// @endcode
class CombinedServerRunner final
{
public:
  /// Constructs a runner with a ServerFactory and options.
  CombinedServerRunner(ServerFactory serverFactory, CombinedServerRunnerOptions options);

  ~CombinedServerRunner();

  CombinedServerRunner(const CombinedServerRunner &) = delete;
  auto operator=(const CombinedServerRunner &) -> CombinedServerRunner & = delete;
  CombinedServerRunner(CombinedServerRunner &&other) noexcept;
  auto operator=(CombinedServerRunner &&other) noexcept -> CombinedServerRunner &;

  /// Starts all enabled transports.
  ///
  /// If enableHttp is true, starts the HTTP server (non-blocking).
  /// If enableStdio is true, runs the STDIO transport (blocking).
  ///
  /// Note: When both transports are enabled, STDIO runs in the current thread
  /// while HTTP runs in the background. The call blocks until STDIO closes.
  auto start() -> void;

  /// Stops all enabled transports.
  ///
  /// If HTTP is running, stops the HTTP server.
  /// This does not affect STDIO (which exits when stdin closes).
  auto stop() -> void;

  /// Runs the STDIO transport (blocking).
  ///
  /// This method blocks until EOF is reached on stdin or an error occurs.
  /// Only available if enableStdio is true in the options.
  auto runStdio() -> void;

  /// Starts only the HTTP transport (non-blocking).
  ///
  /// Only available if enableHttp is true in the options.
  /// After calling this, use localPort() to determine the actual port.
  auto startHttp() -> void;

  /// Stops only the HTTP transport.
  ///
  /// Only available if enableHttp is true in the options.
  auto stopHttp() -> void;

  /// Returns whether the HTTP server is currently running.
  [[nodiscard]] auto isHttpRunning() const noexcept -> bool;

  /// Returns the local port the HTTP server is listening on.
  ///
  /// @returns The port number, or 0 if HTTP is not running.
  [[nodiscard]] auto localPort() const noexcept -> std::uint16_t;

  /// Returns the options used by this runner.
  [[nodiscard]] auto options() const -> const CombinedServerRunnerOptions &;

  /// Returns access to the STDIO runner (if enabled).
  ///
  /// @returns Pointer to the STDIO runner, or nullptr if not enabled.
  [[nodiscard]] auto stdioRunner() -> StdioServerRunner *;

  /// Returns access to the HTTP runner (if enabled).
  ///
  /// @returns Pointer to the HTTP runner, or nullptr if not enabled.
  [[nodiscard]] auto httpRunner() -> StreamableHttpServerRunner *;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp
