#pragma once

#include <string>

#include <mcp/auth/oauth_server.hpp>

namespace mcp::auth
{

struct OAuthAccessToken
{
  std::string value;
  OAuthScopeSet scopes;
};

}  // namespace mcp::auth
