#pragma once

#include <functional>

#include <mcp/auth/oauth_http_response.hpp>
#include <mcp/auth/oauth_token_http_request.hpp>

namespace mcp::auth
{

using OAuthHttpRequestExecutor = std::function<OAuthHttpResponse(const OAuthTokenHttpRequest &request)>;

}  // namespace mcp::auth
