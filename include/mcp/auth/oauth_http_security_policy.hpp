#pragma once

#include <cstddef>

namespace mcp::auth
{

struct OAuthHttpSecurityPolicy
{
  bool requireHttps = true;
  bool requireSameOriginRedirects = true;
  std::size_t maxRedirects = 2;
};

}  // namespace mcp::auth
