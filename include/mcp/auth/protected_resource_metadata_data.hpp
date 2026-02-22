#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mcp/auth/oauth_server.hpp>

namespace mcp::auth
{

struct ProtectedResourceMetadata
{
  std::string resource;
  std::vector<std::string> authorizationServers;
  std::optional<OAuthScopeSet> scopesSupported;
};

}  // namespace mcp::auth
