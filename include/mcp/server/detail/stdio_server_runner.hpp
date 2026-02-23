#pragma once

#include <atomic>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <mcp/server/detail/server_factory.hpp>
#include <mcp/server/detail/stdio_server_runner_options.hpp>

namespace mcp::server
{

/// @section Exceptions
///
/// The StdioServerRunner provides the following exception guarantees:
/// - Constructor: Does not throw
/// - Destructor: noexcept (guaranteed not to throw)
/// - Move operations: noexcept
/// - run(): May throw std::runtime_error on fatal errors (typically not recoverable)
/// - run(istream, ostream, ostream): May throw std::runtime_error on fatal errors
/// - startAsync(): Does not throw; returns std::thread which may throw on join
/// - stop() noexcept: Idempotent; never throws
/// - options() noexcept: Safe to call from any thread
///
/// @par Threading Guarantees
/// - run() is blocking and runs on the calling thread
/// - startAsync() creates a background thread
/// - stop() can be called from any thread to signal termination
/// - The background thread created by startAsync() has a noexcept entrypoint
/// - All exceptions in the background thread are caught and reported via ErrorReporter

/// Runner for serving MCP over STDIO.
///
/// This runner provides a simple blocking API for running an MCP server
/// over standard input/output. It handles the transport lifecycle and
/// uses a ServerFactory to create Server instances.
///
/// By default, logs are written to stderr to avoid polluting stdout
/// which is reserved for JSON-RPC messages.
///
/// The runner creates exactly one Server instance for its lifetime,
/// calls server->start() before processing messages, and calls
/// server->stop() when the runner exits.
///
/// Usage:
/// @code
///   ServerFactory makeServer = [] { return mcp::server::Server::create(); };
///   mcp::server::StdioServerRunner runner(makeServer);
///   runner.run();
/// @endcode
///
/// Or with custom options:
/// @code
///   StdioServerRunnerOptions options;
///   options.transportOptions.allowStderrLogs = true;
///   ServerFactory makeServer = [] { return mcp::server::Server::create(); };
///   mcp::server::StdioServerRunner runner(makeServer, options);
///   runner.run();
/// @endcode
///
/// For async usage:
/// @code
///   ServerFactory makeServer = [] { return mcp::server::Server::create(); };
///   mcp::server::StdioServerRunner runner(makeServer);
///   auto thread = runner.startAsync();
///   // ... do other work ...
///   runner.stop();
///   thread.join();
/// @endcode
class StdioServerRunner final
{
public:
  /// Constructs a runner with a ServerFactory and default options.
  explicit StdioServerRunner(ServerFactory serverFactory);

  /// Constructs a runner with a ServerFactory and custom options.
  StdioServerRunner(ServerFactory serverFactory, StdioServerRunnerOptions options);

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
  /// @param input       Stream to read JSON-RPC messages from (default: std::cin)
  /// @param output      Stream to write JSON-RPC messages to (default: std::cout)
  /// @param errorStream Stream to write log messages to (default: std::cerr)
  auto run(std::istream &input, std::ostream &output, std::ostream &errorStream) -> void;

  /// Starts the server asynchronously on a joinable thread.
  ///
  /// This method runs the server in a separate thread, allowing the calling
  /// thread to continue execution. The server thread will run until EOF is
  /// reached on stdin or stop() is called.
  ///
  /// To stop the server, call stop() which sets an atomic flag. The host is
  /// responsible for closing the input stream to unblock blocking reads.
  ///
  /// @note The returned thread must be joined by the caller.
  [[nodiscard]] auto startAsync() -> std::thread;

  /// Requests the server to stop.
  ///
  /// This method sets an atomic flag that signals the server loop to terminate.
  /// The host must close the input stream to unblock any blocking reads.
  ///
  /// @note This does not block; it only signals the server thread to stop.
  auto stop() noexcept -> void;

  /// Returns the options used by this runner.
  [[nodiscard]] auto options() const -> const StdioServerRunnerOptions &;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::server
