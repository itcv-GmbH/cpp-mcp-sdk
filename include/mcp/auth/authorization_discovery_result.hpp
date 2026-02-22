#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mcp/auth/authorization_server_metadata.hpp>
#include <mcp/auth/bearer_www_authenticate_challenge.hpp>
#include <mcp/auth/oauth_server.hpp>
#include <mcp/auth/protected_resource_metadata_data.hpp>

namespace mcp::auth
{

struct AuthorizationDiscoveryResult
{
  std::vector<BearerWwwAuthenticateChallenge> bearerChallenges;
  std::optional<BearerWwwAuthenticateChallenge> selectedBearerChallenge;
  std::string protectedResourceMetadataUrl;
  ProtectedResourceMetadata protectedResourceMetadata;
  std::string selectedAuthorizationServer;
  std::string authorizationServerMetadataUrl;
  AuthorizationServerMetadata authorizationServerMetadata;
  std::optional<OAuthScopeSet> selectedScopes;
};

}  // namespace mcp::auth
