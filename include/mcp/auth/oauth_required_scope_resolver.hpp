#pragma once

#include <functional>

#include <mcp/auth/oauth_authorization_request_context.hpp>
#include <mcp/auth/oauth_scope_set.hpp>

namespace mcp::auth
{

using OAuthRequiredScopeResolver = std::function<OAuthScopeSet(const OAuthAuthorizationRequestContext &)>;

}  // namespace mcp::auth
