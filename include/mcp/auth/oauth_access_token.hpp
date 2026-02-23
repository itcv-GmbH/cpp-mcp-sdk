#pragma once

#include <string>

#include <mcp/auth/oauth_scope_set.hpp>

namespace mcp::auth
{

struct OAuthAccessToken
{
  std::string value;
  OAuthScopeSet scopes;
};

}  // namespace mcp::auth
