#pragma once

#include <cstddef>

namespace mcp::auth
{

struct DiscoverySecurityPolicy
{
  bool requireHttps = true;
  bool requireSameOriginRedirects = true;
  bool allowPrivateAndLocalAddresses = false;
  std::size_t maxRedirects = 4;
};

}  // namespace mcp::auth
