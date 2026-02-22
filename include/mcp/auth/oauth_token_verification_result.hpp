#pragma once

#include <mcp/auth/oauth_authorization_context.hpp>
#include <mcp/auth/oauth_token_verification_status.hpp>

namespace mcp::auth
{

struct OAuthTokenVerificationResult
{
  OAuthTokenVerificationStatus status = OAuthTokenVerificationStatus::kInvalidToken;
  bool audienceBound = false;
  OAuthAuthorizationContext authorizationContext;
};

}  // namespace mcp::auth
