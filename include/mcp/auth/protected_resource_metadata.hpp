#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/auth/oauth_server.hpp>

namespace mcp::auth
{

enum class AuthorizationDiscoveryErrorCode : std::uint8_t
{
  kInvalidInput,
  kSecurityPolicyViolation,
  kNetworkFailure,
  kMetadataValidation,
  kNotFound,
};

class AuthorizationDiscoveryError : public std::runtime_error
{
public:
  AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode code, std::string message);

  [[nodiscard]] auto code() const noexcept -> AuthorizationDiscoveryErrorCode;

private:
  AuthorizationDiscoveryErrorCode code_;
};

struct BearerWwwAuthenticateParameter
{
  std::string name;
  std::string value;
};

struct BearerWwwAuthenticateChallenge
{
  std::optional<std::string> resourceMetadata;
  std::optional<std::string> scope;
  std::optional<std::string> error;
  std::vector<BearerWwwAuthenticateParameter> parameters;
};

struct ProtectedResourceMetadata
{
  std::string resource;
  std::vector<std::string> authorizationServers;
  std::optional<OAuthScopeSet> scopesSupported;
};

struct AuthorizationServerMetadata
{
  std::string issuer;
  std::optional<std::string> authorizationEndpoint;
  std::optional<std::string> tokenEndpoint;
  std::optional<std::string> registrationEndpoint;
  std::vector<std::string> codeChallengeMethodsSupported;
  bool clientIdMetadataDocumentSupported = false;
};

struct DiscoveryHeader
{
  std::string name;
  std::string value;
};

struct DiscoveryHttpRequest
{
  std::string method = "GET";
  std::string url;
  std::vector<DiscoveryHeader> headers;
};

struct DiscoveryHttpResponse
{
  std::uint16_t statusCode = 0;
  std::vector<DiscoveryHeader> headers;
  std::string body;
};

using DiscoveryHttpFetcher = std::function<DiscoveryHttpResponse(const DiscoveryHttpRequest &)>;
using DiscoveryDnsResolver = std::function<std::vector<std::string>(std::string_view host)>;

struct DiscoverySecurityPolicy
{
  bool requireHttps = true;
  bool requireSameOriginRedirects = true;
  bool allowPrivateAndLocalAddresses = false;
  std::size_t maxRedirects = 4;
};

struct AuthorizationDiscoveryRequest
{
  std::string mcpEndpointUrl;
  std::vector<std::string> wwwAuthenticateHeaderValues;
  DiscoveryHttpFetcher httpFetcher;
  DiscoveryDnsResolver dnsResolver;
  DiscoverySecurityPolicy securityPolicy;
};

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

auto parseBearerWwwAuthenticateChallenges(const std::vector<std::string> &headerValues) -> std::vector<BearerWwwAuthenticateChallenge>;
auto parseProtectedResourceMetadata(std::string_view jsonDocument) -> ProtectedResourceMetadata;
auto parseAuthorizationServerMetadata(std::string_view jsonDocument) -> AuthorizationServerMetadata;
auto selectScopesForAuthorization(const std::vector<BearerWwwAuthenticateChallenge> &bearerChallenges, const ProtectedResourceMetadata &metadata) -> std::optional<OAuthScopeSet>;
auto discoverAuthorizationMetadata(const AuthorizationDiscoveryRequest &request) -> AuthorizationDiscoveryResult;

}  // namespace mcp::auth
