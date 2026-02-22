#pragma once

#include <string>
#include <vector>

#include <mcp/error_reporter.hpp>
#include <mcp/transport/stdio_client_stderr_mode.hpp>

namespace mcp::transport
{

struct StdioSubprocessSpawnOptions
{
  std::vector<std::string> argv;
  std::vector<std::string> envOverrides;
  std::string cwd;
  StdioClientStderrMode stderrMode = StdioClientStderrMode::kCapture;
  /// Error reporter callback for background thread failures.
  /// If not set, errors are silently suppressed.
  ErrorReporter errorReporter;
};

}  // namespace mcp::transport
