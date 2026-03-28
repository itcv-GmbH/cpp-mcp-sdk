#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mcp/auth/authorization_discovery_request.hpp>
#include <mcp/auth/authorization_server_metadata.hpp>
#include <mcp/auth/bearer_www_authenticate_challenge.hpp>
#include <mcp/auth/oauth_scope_set.hpp>
#include <mcp/auth/protected_resource_metadata_data.hpp>
#include <mcp/export.hpp>

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
MCP_SDK_EXPORT auto parseBearerWwwAuthenticateChallenges(const std::vector<std::string> &headerValues) -> std::vector<BearerWwwAuthenticateChallenge>;
MCP_SDK_EXPORT auto parseProtectedResourceMetadata(std::string_view jsonDocument) -> ProtectedResourceMetadata;
MCP_SDK_EXPORT auto parseAuthorizationServerMetadata(std::string_view jsonDocument) -> AuthorizationServerMetadata;
MCP_SDK_EXPORT auto selectScopesForAuthorization(const std::vector<BearerWwwAuthenticateChallenge> &bearerChallenges, const ProtectedResourceMetadata &metadata) -> std::optional<OAuthScopeSet>;
MCP_SDK_EXPORT auto discoverAuthorizationMetadata(const AuthorizationDiscoveryRequest &request) -> AuthorizationDiscoveryResult;

}  // namespace mcp::auth
