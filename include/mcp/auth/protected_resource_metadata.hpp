#pragma once

// Umbrella header for protected resource metadata and authorization discovery types.
// This header includes all per-type headers for convenient access.

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/auth/authorization_discovery_error.hpp>
#include <mcp/auth/authorization_discovery_request.hpp>
#include <mcp/auth/authorization_discovery_result.hpp>
#include <mcp/auth/authorization_server_metadata.hpp>
#include <mcp/auth/bearer_www_authenticate_challenge.hpp>
#include <mcp/auth/bearer_www_authenticate_parameter.hpp>
#include <mcp/auth/discovery_header.hpp>
#include <mcp/auth/discovery_http_request.hpp>
#include <mcp/auth/discovery_http_response.hpp>
#include <mcp/auth/discovery_http_types.hpp>
#include <mcp/auth/discovery_security_policy.hpp>
#include <mcp/auth/oauth_server.hpp>
#include <mcp/auth/protected_resource_metadata_data.hpp>

namespace mcp::auth
{

// Free functions for parsing and authorization discovery
auto parseBearerWwwAuthenticateChallenges(const std::vector<std::string> &headerValues) -> std::vector<BearerWwwAuthenticateChallenge>;
auto parseProtectedResourceMetadata(std::string_view jsonDocument) -> ProtectedResourceMetadata;
auto parseAuthorizationServerMetadata(std::string_view jsonDocument) -> AuthorizationServerMetadata;
auto selectScopesForAuthorization(const std::vector<BearerWwwAuthenticateChallenge> &bearerChallenges, const ProtectedResourceMetadata &metadata) -> std::optional<OAuthScopeSet>;
auto discoverAuthorizationMetadata(const AuthorizationDiscoveryRequest &request) -> AuthorizationDiscoveryResult;

}  // namespace mcp::auth
