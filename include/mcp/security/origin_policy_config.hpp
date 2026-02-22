#pragma once

#include <string>
#include <vector>

namespace mcp::security
{

struct OriginPolicy
{
  bool validateOrigin = true;
  bool allowRequestsWithoutOrigin = true;
  std::vector<std::string> allowedOrigins;
  std::vector<std::string> allowedHosts = {
    "localhost",
    "127.0.0.1",
    "::1",
    "[::1]",
  };
};

}  // namespace mcp::security
