#pragma once

#include <memory>
#include <optional>

#include <mcp/server/server.hpp>
#include <mcp/server/stdio_runner.hpp>
#include <mcp/server/streamable_http_runner.hpp>

namespace mcp
{

/// Flags to control which transports a CombinedServerRunner should enable.
struct CombinedServerRunnerOptions
{
  /// Options for the STDIO transport.
  /// Only used if enableStdio is true.
  StdioServerRunnerOptions stdioOptions;

  /// Options for the Streamable HTTP transport.
  /// Only used if enableHttp is true.
  StreamableHttpServerRunnerOptions httpOptions;

  /// Whether to enable STDIO transport.
  bool enableStdio = false;

  /// Whether to enable Streamable HTTP transport.
  bool enableHttp = false;
};

/// Combined runner for serving MCP over multiple transports.
///
/// This runner supports running an MCP server over STDIO, Streamable HTTP,
/// or both simultaneously. It provides a unified API for managing multiple
/// transports.
///
/// In STDIO-only mode, a single Server instance is used.
/// In HTTP-only mode, a single Server instance is used.
/// In dual-transport mode with session isolation, each transport maintains
/// its own Server instance to ensure proper session isolation between
/// STDIO and HTTP clients.
///
/// Usage (STDIO only):
/// @code
///   CombinedServerRunnerOptions options;
///   options.enableStdio = true;
///   CombinedServerRunner runner(options);
///   runner.runStdio();
/// @endcode
///
/// Usage (HTTP only):
/// @code
///   CombinedServerRunnerOptions options;
///   options.enableHttp = true;
///   options.httpOptions.transportOptions.http.endpoint.port = 8080;
///   CombinedServerRunner runner(options);
///   runner.startHttp();
/// @endcode
///
/// Usage (both STDIO and HTTP):
/// @code
///   CombinedServerRunnerOptions options;
///   options.enableStdio = true;
///   options.enableHttp = true;
///   options.httpOptions.transportOptions.http.requireSessionId = true;
///   CombinedServerRunner runner(options);
///   runner.start();  // Starts both transports
///   // ... server is handling requests ...
///   runner.stop();  // Stops both transports
/// @endcode
class CombinedServerRunner final
{
public:
  /// Constructs a runner with the given options.
  explicit CombinedServerRunner(CombinedServerRunnerOptions options);

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

  /// Returns the server instance used by this runner.
  /// Can be used to register tools, resources, prompts, etc. before calling start().
  [[nodiscard]] auto server() const -> std::shared_ptr<Server>;

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
