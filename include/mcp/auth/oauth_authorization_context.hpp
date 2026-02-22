#pragma once

#include <optional>
#include <string>

#include <mcp/auth/oauth_scope_set.hpp>

namespace mcp::auth
{

struct OAuthAuthorizationContext
{
  std::string taskIsolationKey;
  std::optional<std::string> subject;
  OAuthScopeSet grantedScopes;
};

}  // namespace mcp::auth
