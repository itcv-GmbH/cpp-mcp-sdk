#pragma once

#include <mcp/security/limits.hpp>

namespace mcp::transport
{

struct StdioServerOptions
{
  bool allowStderrLogs = true;
  security::RuntimeLimits limits;
};

}  // namespace mcp::transport
