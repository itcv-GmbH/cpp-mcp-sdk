#pragma once

#include <functional>

#include <mcp/auth/oauth_http_response.hpp>
#include <mcp/auth/oauth_protected_resource_request.hpp>

namespace mcp::auth
{

using OAuthProtectedResourceRequestExecutor = std::function<OAuthHttpResponse(const OAuthProtectedResourceRequest &request)>;

}  // namespace mcp::auth
