#pragma once

#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <mcp/server/server.hpp>
#include <mcp/transport/stdio.hpp>

namespace mcp
{

/// Configuration options for the STDIO server runner.
struct StdioServerRunnerOptions
{
  /// Options passed through to the underlying stdio transport.
  /// Controls behavior like stderr logging and runtime limits.
  transport::StdioServerOptions transportOptions;

  /// Optional server configuration. If not provided, a default Server
  /// will be created with no capabilities registered.
  std::optional<ServerConfiguration> serverConfiguration;
};

/// Runner for serving MCP over STDIO.
///
/// This runner provides a simple blocking API for running an MCP server
/// over standard input/output. It handles the transport lifecycle and
/// delegates to a Server instance for protocol handling.
///
/// By default, logs are written to stderr to avoid polluting stdout
/// which is reserved for JSON-RPC messages.
///
/// Usage:
/// @code
///   StdioServerRunner runner;
///   runner.run();
/// @endcode
///
/// Or with custom options:
/// @code
///   StdioServerRunnerOptions options;
///   options.transportOptions.allowStderrLogs = true;
///   StdioServerRunner runner(options);
///   runner.run();
/// @endcode
class StdioServerRunner final
{
public:
  /// Constructs a runner with default options.
  StdioServerRunner();

  /// Constructs a runner with custom options.
  explicit StdioServerRunner(StdioServerRunnerOptions options);

  ~StdioServerRunner();

  StdioServerRunner(const StdioServerRunner &) = delete;
  auto operator=(const StdioServerRunner &) -> StdioServerRunner & = delete;
  StdioServerRunner(StdioServerRunner &&other) noexcept;
  auto operator=(StdioServerRunner &&other) noexcept -> StdioServerRunner &;

  /// Runs the server synchronously, reading JSON-RPC messages from stdin
  /// and writing responses to stdout.
  ///
  /// This method blocks until EOF is reached on stdin or an error occurs.
  /// Log messages are written to stderr by default.
  auto run() -> void;

  /// Runs the server with custom input/output/error streams.
  ///
  /// @param input  Stream to read JSON-RPC messages from (default: std::cin)
  /// @param output Stream to write JSON-RPC messages to (default: std::cout)
  /// @param error  Stream to write log messages to (default: std::cerr)
  auto run(std::istream &input, std::ostream &output, std::ostream &error) -> void;

  /// Returns the server instance used by this runner.
  /// Can be used to register tools, resources, prompts, etc. before calling run().
  [[nodiscard]] auto server() const -> std::shared_ptr<Server>;

  /// Returns the options used by this runner.
  [[nodiscard]] auto options() const -> const StdioServerRunnerOptions &;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp
