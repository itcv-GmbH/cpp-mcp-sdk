#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mcp::auth
{

struct OAuthScopeSet
{
  std::vector<std::string> values;
};

struct OAuthProtectedResourceMetadata
{
  std::string resource;
  std::vector<std::string> authorizationServers;
  OAuthScopeSet scopesSupported;
};

struct OAuthProtectedResourceMetadataPublication
{
  bool publishAtPathBasedWellKnownUri = true;
  bool publishAtRootWellKnownUri = true;
  std::optional<std::string> challengeResourceMetadataUrl;
};

struct OAuthAuthorizationRequestContext
{
  std::string httpMethod;
  std::string httpPath;
  std::optional<std::string> sessionId;
};

struct OAuthTokenVerificationRequest
{
  std::string bearerToken;
  std::string expectedAudience;
  OAuthAuthorizationRequestContext request;
  OAuthScopeSet requiredScopes;
};

struct OAuthAuthorizationContext
{
  std::string taskIsolationKey;
  std::optional<std::string> subject;
  OAuthScopeSet grantedScopes;
};

enum class OAuthTokenVerificationStatus : std::uint8_t
{
  kValid,
  kInvalidToken,
  kInsufficientScope,
};

struct OAuthTokenVerificationResult
{
  OAuthTokenVerificationStatus status = OAuthTokenVerificationStatus::kInvalidToken;
  bool audienceBound = false;
  OAuthAuthorizationContext authorizationContext;
};

class OAuthTokenVerifier
{
public:
  virtual ~OAuthTokenVerifier() = default;
  virtual auto verifyToken(const OAuthTokenVerificationRequest &request) const -> OAuthTokenVerificationResult = 0;
};

using OAuthRequiredScopeResolver = std::function<OAuthScopeSet(const OAuthAuthorizationRequestContext &)>;

struct OAuthServerAuthorizationOptions
{
  std::shared_ptr<const OAuthTokenVerifier> tokenVerifier;
  OAuthProtectedResourceMetadata protectedResourceMetadata;
  OAuthProtectedResourceMetadataPublication metadataPublication;
  OAuthRequiredScopeResolver requiredScopesResolver;
  OAuthScopeSet defaultRequiredScopes;
};

}  // namespace mcp::auth
