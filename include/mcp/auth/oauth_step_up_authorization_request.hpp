#pragma once

#include <optional>
#include <string>

#include <mcp/auth/oauth_server.hpp>

namespace mcp::auth
{

struct OAuthStepUpAuthorizationRequest
{
  std::string resource;
  OAuthScopeSet requestedScopes;
  std::optional<std::string> resourceMetadataUrl;
};

}  // namespace mcp::auth
