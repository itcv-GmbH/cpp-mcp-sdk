#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <mcp/auth/discovery_header.hpp>

namespace mcp::auth
{

struct DiscoveryHttpResponse
{
  std::uint16_t statusCode = 0;
  std::vector<DiscoveryHeader> headers;
  std::string body;
};

}  // namespace mcp::auth
