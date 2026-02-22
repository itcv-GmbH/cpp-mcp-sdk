#pragma once

#include <functional>

#include <mcp/auth/oauth_access_token.hpp>
#include <mcp/auth/oauth_step_up_authorization_request.hpp>

namespace mcp::auth
{

using OAuthStepUpAuthorizer = std::function<OAuthAccessToken(const OAuthStepUpAuthorizationRequest &request)>;

}  // namespace mcp::auth
