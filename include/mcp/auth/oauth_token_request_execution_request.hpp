#pragma once

#include <mcp/auth/oauth_http_request_executor.hpp>
#include <mcp/auth/oauth_http_security_policy.hpp>
#include <mcp/auth/oauth_token_http_request.hpp>

namespace mcp::auth
{

struct OAuthTokenRequestExecutionRequest
{
  OAuthTokenHttpRequest tokenRequest;
  OAuthHttpRequestExecutor requestExecutor;
  OAuthHttpSecurityPolicy securityPolicy;
};

}  // namespace mcp::auth
