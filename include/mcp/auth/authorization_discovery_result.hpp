#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mcp/auth/authorization_discovery_request.hpp>
#include <mcp/auth/authorization_server_metadata.hpp>
#include <mcp/auth/bearer_www_authenticate_challenge.hpp>
#include <mcp/auth/oauth_scope_set.hpp>
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

// Free functions for parsing and authorization discovery
auto parseBearerWwwAuthenticateChallenges(const std::vector<std::string> &headerValues) -> std::vector<BearerWwwAuthenticateChallenge>;
auto parseProtectedResourceMetadata(std::string_view jsonDocument) -> ProtectedResourceMetadata;
auto parseAuthorizationServerMetadata(std::string_view jsonDocument) -> AuthorizationServerMetadata;
auto selectScopesForAuthorization(const std::vector<BearerWwwAuthenticateChallenge> &bearerChallenges, const ProtectedResourceMetadata &metadata) -> std::optional<OAuthScopeSet>;
auto discoverAuthorizationMetadata(const AuthorizationDiscoveryRequest &request) -> AuthorizationDiscoveryResult;

}  // namespace mcp::auth
