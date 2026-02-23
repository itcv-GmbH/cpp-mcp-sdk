#pragma once

#include <mcp/sdk/error_reporter.hpp>
#include <mcp/transport/all.hpp>

namespace mcp::server
{

/// Configuration options for the STDIO server runner.
struct StdioServerRunnerOptions
{
  /// Options passed through to the underlying stdio transport.
  /// Controls behavior like stderr logging and runtime limits.
  transport::StdioServerOptions transportOptions;

  /// Error reporter callback for background execution context failures.
  /// If not set, errors are silently suppressed.
  ErrorReporter errorReporter;
};

}  // namespace mcp::server
