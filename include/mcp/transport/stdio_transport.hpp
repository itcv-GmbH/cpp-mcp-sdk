#pragma once

#include <iosfwd>
#include <string_view>

#include <mcp/export.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/transport/stdio_attach_options.hpp>
#include <mcp/transport/stdio_client_options.hpp>
#include <mcp/transport/stdio_server_options.hpp>
#include <mcp/transport/stdio_subprocess.hpp>
#include <mcp/transport/stdio_subprocess_spawn_options.hpp>
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
 * @par Concurrency Rules:
 * 1. For static StdioTransport::run(), only one invocation should be active per process
 *    for the same streams.
 * 2. For StdioSubprocess, external synchronization is required if accessed from multiple
 *    threads concurrently.
 *
 * @section Exceptions
 *
 * @subsection StdioTransport (Static Methods)
 * - run(): Throws std::runtime_error on I/O error or protocol error
 * - attach(): Throws std::runtime_error on attach failure
 * - routeIncomingLine(): Returns bool success
 * - spawnSubprocess(): Throws std::runtime_error on subprocess spawn failure
 *
 * @subsection Deprecated Instance Methods
 * The following instance methods are deprecated and throw when called:
 * - StdioTransport(StdioServerOptions) constructor: throws std::runtime_error
 * - StdioTransport(const StdioClientOptions&) constructor: throws std::runtime_error
 * - attach(std::weak_ptr<Session>): throws std::runtime_error
 * - start(): throws std::runtime_error
 * - stop(): throws std::runtime_error
 * - isRunning() const noexcept: Returns false (deprecated marker)
 * - send(): throws std::runtime_error
 *
 * Users should migrate to:
 * - For servers: Use static StdioTransport::run()
 * - For clients: Use mcp::Client::connectStdio() or StdioTransport::spawnSubprocess()
 *
 * @subsection Exception Behavior
 * The static run() and attach() methods handle exceptions from:
 * - User-provided router handlers (suppressed, may be logged)
 * - Message parsing errors (logged, loop continues)
 * - Subprocess I/O errors (terminates loop)
 */
class MCP_SDK_EXPORT StdioTransport final : public Transport
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
