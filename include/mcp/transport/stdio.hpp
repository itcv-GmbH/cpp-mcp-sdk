#pragma once

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/jsonrpc/router.hpp>
#include <mcp/security/limits.hpp>
#include <mcp/transport/transport.hpp>

namespace mcp::transport
{

/**
 * @brief STDIO transport implementation for MCP SDK.
 *
 * @section Thread Safety
 *
 * This header defines STDIO transport types with the following thread-safety classifications:
 *
 * @par StdioTransport - Thread-compatible (deprecated instance methods)
 *
 * The StdioTransport class has deprecated instance-based methods in favor of static methods.
 *
 * Static methods (thread-safe):
 * - run() - Blocking, runs until EOF
 * - attach() - Blocking, runs until EOF
 * - routeIncomingLine() - Thread-safe
 * - spawnSubprocess() - Thread-safe
 *
 * @par StdioSubprocess - Thread-compatible
 * - Designed for single-threaded or externally synchronized use
 * - Methods that modify state (writeLine(), readLine(), closeStdin(), shutdown())
 *   must not be called concurrently
 * - Query methods (valid(), isRunning(), exitCode(), capturedStderr()) are thread-safe
 *
 * @par Concurrency Rules:
 * 1. For static StdioTransport::run(), only one invocation should be active per process
 *    for the same streams.
 * 2. For StdioSubprocess, external synchronization is required if accessed from multiple
 *    threads concurrently.
 *
 * @section Exceptions
 *
 * @subsection StdioSubprocess
 * - Constructor: Does not throw
 * - Destructor: noexcept, safe cleanup of subprocess resources
 * - Move operations: noexcept
 * - valid(): noexcept
 * - writeLine(): Throws std::runtime_error on write failure or broken pipe
 * - readLine(): Returns bool success, may throw std::runtime_error on I/O error
 * - closeStdin(): noexcept
 * - waitForExit(): Returns bool, does not throw (uses timeout)
 * - shutdown(): noexcept, returns bool success/failure
 * - isRunning(): Returns bool, does not throw
 * - exitCode(): Returns std::optional<int>, does not throw
 * - capturedStderr(): Returns std::string, does not throw
 *
 * @subsection StdioTransport (Static Methods)
 * - run(): Throws std::runtime_error on I/O error or protocol error
 * - attach(): Throws std::runtime_error on attach failure
 * - routeIncomingLine(): Returns bool success, exceptions from router are contained
 * - spawnSubprocess(): Throws std::runtime_error on subprocess spawn failure
 *
 * @subsection Deprecated Instance Methods
 * The following instance methods are deprecated and throw when called:
 * - StdioTransport(StdioServerOptions) constructor: throws std::runtime_error
 * - StdioTransport(const StdioClientOptions&) constructor: throws std::runtime_error
 * - attach(std::weak_ptr<Session>): throws std::runtime_error
 * - start(): throws std::runtime_error
 * - stop(): throws std::runtime_error
 * - isRunning(): const noexcept: Returns false (deprecated marker)
 * - send(): throws std::runtime_error
 *
 * Users should migrate to:
 * - For servers: Use static StdioTransport::run()
 * - For clients: Use mcp::Client::connectStdio() or StdioTransport::spawnSubprocess()
 *
 * @subsection Exception Containment
 * The static run() and attach() methods contain exceptions from:
 * - User-provided router handlers (converted to error responses or logged)
 * - Message parsing errors (logged, loop continues)
 * - Subprocess I/O errors (terminates loop gracefully)
 *
 * @subsection Thread Safety Notes
 * - StdioSubprocess: Not thread-safe; external synchronization required
 * - Static transport methods: Thread-safe for distinct router instances
 */
inline constexpr std::int64_t kDefaultStdioShutdownTimeoutMilliseconds = 1500;

struct StdioServerOptions
{
  bool allowStderrLogs = true;
  security::RuntimeLimits limits;
};

struct StdioClientOptions
{
  std::string executablePath;
  std::vector<std::string> arguments;
  std::vector<std::string> environment;
  security::RuntimeLimits limits;
};

enum class StdioClientStderrMode : std::uint8_t
{
  kCapture,
  kForward,
  kIgnore,
};

struct StdioSubprocessSpawnOptions
{
  std::vector<std::string> argv;
  std::vector<std::string> envOverrides;
  std::string cwd;
  StdioClientStderrMode stderrMode = StdioClientStderrMode::kCapture;
};

struct StdioSubprocessShutdownOptions
{
  std::chrono::milliseconds waitForExitTimeout {kDefaultStdioShutdownTimeoutMilliseconds};
  std::chrono::milliseconds waitAfterTerminateTimeout {kDefaultStdioShutdownTimeoutMilliseconds};
};

class StdioSubprocess final
{
public:
  StdioSubprocess();
  ~StdioSubprocess();

  StdioSubprocess(const StdioSubprocess &) = delete;
  auto operator=(const StdioSubprocess &) -> StdioSubprocess & = delete;
  StdioSubprocess(StdioSubprocess &&other) noexcept;
  auto operator=(StdioSubprocess &&other) noexcept -> StdioSubprocess &;

  [[nodiscard]] auto valid() const noexcept -> bool;
  auto writeLine(std::string_view line) -> void;
  auto readLine(std::string &line) -> bool;
  auto closeStdin() noexcept -> void;
  [[nodiscard]] auto waitForExit(std::chrono::milliseconds timeout) -> bool;
  [[nodiscard]] auto shutdown(StdioSubprocessShutdownOptions options = {}) noexcept -> bool;
  [[nodiscard]] auto isRunning() -> bool;
  [[nodiscard]] auto exitCode() const -> std::optional<int>;
  [[nodiscard]] auto capturedStderr() const -> std::string;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  explicit StdioSubprocess(std::unique_ptr<Impl> impl);
  friend class StdioTransport;
};

struct StdioAttachOptions
{
  bool allowStderrLogs = true;
  bool emitParseErrors = false;
  security::RuntimeLimits limits;
};

class StdioTransport final : public Transport
{
public:
  [[deprecated(
    "StdioTransport instance constructors are deprecated and throw. "
    "For servers, use static StdioTransport::run(). "
    "For clients, use mcp::Client::connectStdio() or StdioTransport::spawnSubprocess().")]] explicit StdioTransport(StdioServerOptions options = {});
  [[deprecated(
    "StdioTransport instance constructors are deprecated and throw. "
    "For servers, use static StdioTransport::run(). "
    "For clients, use mcp::Client::connectStdio() or StdioTransport::spawnSubprocess().")]] explicit StdioTransport(const StdioClientOptions &options);

  [[deprecated(
    "StdioTransport::attach() is deprecated and throws. "
    "For servers, use static StdioTransport::run(). "
    "For clients, use mcp::Client::connectStdio().")]] auto
  attach(std::weak_ptr<Session> session) -> void override;
  [[deprecated(
    "StdioTransport::start() is deprecated and throws. "
    "Use static StdioTransport::run() instead.")]] auto
  start() -> void override;
  [[deprecated(
    "StdioTransport::stop() is deprecated and throws. "
    "Use static StdioTransport::run() instead.")]] auto
  stop() -> void override;
  [[deprecated(
    "StdioTransport::isRunning() is deprecated. "
    "For servers, use static StdioTransport::run(). "
    "For clients, use mcp::Client::connectStdio().")]] auto
  isRunning() const noexcept -> bool override;
  [[deprecated(
    "StdioTransport::send() is deprecated and throws. "
    "For servers, use static StdioTransport::run(). "
    "For clients, use mcp::Client::connectStdio().")]] auto
  send(jsonrpc::Message message) -> void override;

  static auto run(jsonrpc::Router &router, StdioServerOptions options = {}) -> void;
  static auto attach(jsonrpc::Router &router, std::istream &serverStdout, std::ostream &serverStdin, StdioAttachOptions options = {}) -> void;
  static auto routeIncomingLine(jsonrpc::Router &router, std::string_view line, std::ostream &output, std::ostream *stderrOutput, StdioAttachOptions options = {}) -> bool;
  [[nodiscard]] static auto spawnSubprocess(const StdioSubprocessSpawnOptions &options) -> StdioSubprocess;
};

}  // namespace mcp::transport
