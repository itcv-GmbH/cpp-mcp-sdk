#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include <mcp/auth/in_memory_oauth_token_storage.hpp>
#include <mcp/auth/oauth_protected_resource_request.hpp>
#include <mcp/auth/oauth_protected_resource_request_executor.hpp>
#include <mcp/auth/oauth_server.hpp>
#include <mcp/auth/oauth_step_up_authorizer.hpp>
#include <mcp/auth/oauth_token_storage.hpp>

namespace mcp::auth
{

struct OAuthStepUpExecutionRequest
{
  OAuthProtectedResourceRequest protectedResourceRequest;
  std::string resource;
  OAuthScopeSet initialScopes;
  std::shared_ptr<OAuthTokenStorage> tokenStorage = std::make_shared<InMemoryOAuthTokenStorage>();
  OAuthProtectedResourceRequestExecutor requestExecutor;
  OAuthStepUpAuthorizer authorizer;
  std::size_t maxStepUpAttempts = 2;
};

}  // namespace mcp::auth
