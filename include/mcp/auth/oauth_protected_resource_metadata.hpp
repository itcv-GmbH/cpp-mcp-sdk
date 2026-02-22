#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mcp/auth/oauth_scope_set.hpp>

namespace mcp::auth
{

struct OAuthProtectedResourceMetadata
{
  std::string resource;
  std::vector<std::string> authorizationServers;
  OAuthScopeSet scopesSupported;
};

}  // namespace mcp::auth
