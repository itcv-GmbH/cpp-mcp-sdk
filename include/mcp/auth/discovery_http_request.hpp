#pragma once

#include <string>
#include <vector>

#include <mcp/auth/discovery_header.hpp>

namespace mcp::auth
{

struct DiscoveryHttpRequest
{
  std::string method = "GET";
  std::string url;
  std::vector<DiscoveryHeader> headers;
};

}  // namespace mcp::auth
