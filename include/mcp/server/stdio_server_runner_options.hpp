#pragma once

#include <mcp/sdk/error_reporter.hpp>
#include <mcp/transport/all.hpp>

namespace mcp::server
{

struct StdioServerRunnerOptions
{
  transport::StdioServerOptions transportOptions;
  ErrorReporter errorReporter;
};

}  // namespace mcp::server
