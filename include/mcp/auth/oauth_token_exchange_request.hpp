#pragma once

#include <string>
#include <vector>

#include <mcp/auth/oauth_query_parameter.hpp>
#include <mcp/auth/protected_resource_metadata.hpp>

namespace mcp::auth
{

struct OAuthTokenExchangeRequest
{
  AuthorizationServerMetadata authorizationServerMetadata;
  std::string clientId;
  std::string redirectUri;
  std::string authorizationCode;
  std::string codeVerifier;
  std::string resource;
  std::vector<OAuthQueryParameter> additionalParameters;
};

}  // namespace mcp::auth
