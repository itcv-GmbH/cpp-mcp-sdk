#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mcp/auth/oauth_query_parameter.hpp>
#include <mcp/auth/protected_resource_metadata.hpp>

namespace mcp::auth
{

struct OAuthAuthorizationUrlRequest
{
  AuthorizationServerMetadata authorizationServerMetadata;
  std::string clientId;
  std::string redirectUri;
  std::string state;
  std::string codeChallenge;
  std::optional<OAuthScopeSet> scopes;
  std::string resource;
  std::vector<OAuthQueryParameter> additionalParameters;
};

}  // namespace mcp::auth
