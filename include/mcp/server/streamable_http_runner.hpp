#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <mcp/server/server.hpp>
#include <mcp/transport/http.hpp>

namespace mcp::server
{

/// Configuration options for the Streamable HTTP server runner.
struct StreamableHttpServerRunnerOptions
{
  /// Options passed through to the underlying Streamable HTTP transport.
  /// Controls endpoint configuration, TLS, authorization, etc.
  transport::http::StreamableHttpServerOptions transportOptions;

  /// Optional server configuration. If not provided, a default Server
  /// will be created with no capabilities registered.
  std::optional<ServerConfiguration> serverConfiguration;
};

/// Runner for serving MCP over Streamable HTTP.
///
/// This runner provides a start/stop API for running an MCP server
/// over HTTP with SSE streaming support. It owns the HTTP server runtime
/// and manages the transport lifecycle.
///
/// For multi-client safety, use `transportOptions.http.requireSessionId = true`.
/// This ensures each client gets a unique session ID, enabling proper request
/// routing and session isolation. See task-002 for details on the session
/// isolation contract.
///
/// Usage:
/// @code
///   StreamableHttpServerRunner runner;
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
///   StreamableHttpServerRunner runner(options);
///   runner.start();
/// @endcode
class StreamableHttpServerRunner final
{
public:
  /// Constructs a runner with default options.
  StreamableHttpServerRunner();

  /// Constructs a runner with custom options.
  explicit StreamableHttpServerRunner(StreamableHttpServerRunnerOptions options);

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

  /// Returns the server instance used by this runner.
  /// Can be used to register tools, resources, prompts, etc. before calling start().
  [[nodiscard]] auto server() const -> std::shared_ptr<Server>;

  /// Returns the options used by this runner.
  [[nodiscard]] auto options() const -> const StreamableHttpServerRunnerOptions &;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::server
