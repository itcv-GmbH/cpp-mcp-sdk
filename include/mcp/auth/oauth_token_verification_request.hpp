#pragma once

#include <string>

#include <mcp/auth/oauth_authorization_request_context.hpp>
#include <mcp/auth/oauth_scope_set.hpp>

namespace mcp::auth
{

struct OAuthTokenVerificationRequest
{
  std::string bearerToken;
  std::string expectedAudience;
  OAuthAuthorizationRequestContext request;
  OAuthScopeSet requiredScopes;
};

}  // namespace mcp::auth
