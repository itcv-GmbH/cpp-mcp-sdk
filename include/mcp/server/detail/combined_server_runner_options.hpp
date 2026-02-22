#pragma once

#include <iosfwd>
#include <memory>
#include <optional>

#include <mcp/server/detail/stdio_server_runner_options.hpp>
#include <mcp/server/detail/streamable_http_server_runner_options.hpp>

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

  /// Custom input stream for STDIO transport (default: nullptr, uses std::cin).
  /// Only used if enableStdio is true.
  /// @note The caller is responsible for ensuring the stream outlives the runner.
  std::istream *stdioInput = nullptr;

  /// Custom output stream for STDIO transport (default: nullptr, uses std::cout).
  /// Only used if enableStdio is true.
  /// @note The caller is responsible for ensuring the stream outlives the runner.
  std::ostream *stdioOutput = nullptr;

  /// Custom error stream for STDIO transport (default: nullptr, uses std::cerr).
  /// Only used if enableStdio is true.
  /// @note The caller is responsible for ensuring the stream outlives the runner.
  std::ostream *stdioError = nullptr;
};

}  // namespace mcp
