#pragma once

#include <string>
#include <vector>

#include <mcp/security/limits.hpp>

namespace mcp::transport
{

struct StdioClientOptions
{
  std::string executablePath;
  std::vector<std::string> arguments;
  std::vector<std::string> environment;
  security::RuntimeLimits limits;
};

}  // namespace mcp::transport
