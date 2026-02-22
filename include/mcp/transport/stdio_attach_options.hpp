#pragma once

#include <mcp/security/limits.hpp>

namespace mcp::transport
{

struct StdioAttachOptions
{
  bool allowStderrLogs = true;
  bool emitParseErrors = false;
  security::RuntimeLimits limits;
};

}  // namespace mcp::transport
