#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <mcp/auth/protected_resource_metadata.hpp>

namespace mcp::auth
{

enum class OAuthClientErrorCode : std::uint8_t
{
  kInvalidInput,
  kMetadataValidation,
  kSecurityPolicyViolation,
  kCryptoFailure,
  kNetworkFailure,
};

class OAuthClientError : public std::runtime_error
{
public:
  OAuthClientError(OAuthClientErrorCode code, const std::string &message);

  [[nodiscard]] auto code() const noexcept -> OAuthClientErrorCode;

private:
  OAuthClientErrorCode code_;
};

inline constexpr std::size_t kDefaultPkceVerifierEntropyBytes = 32U;

struct OAuthQueryParameter
{
  std::string name;
  std::string value;
};

struct OAuthHttpHeader
{
  std::string name;
  std::string value;
};

struct PkceCodePair
{
  std::string codeVerifier;
  std::string codeChallenge;
  std::string codeChallengeMethod = "S256";
};

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

struct OAuthTokenHttpRequest
{
  std::string method = "POST";
  std::string url;
  std::vector<OAuthHttpHeader> headers;
  std::string body;
};

struct OAuthHttpResponse
{
  std::uint16_t statusCode = 0;
  std::vector<OAuthHttpHeader> headers;
  std::string body;
};

using OAuthHttpRequestExecutor = std::function<OAuthHttpResponse(const OAuthTokenHttpRequest &request)>;

struct OAuthHttpSecurityPolicy
{
  bool requireHttps = true;
  bool requireSameOriginRedirects = true;
  std::size_t maxRedirects = 2;
};

struct OAuthTokenRequestExecutionRequest
{
  OAuthTokenHttpRequest tokenRequest;
  OAuthHttpRequestExecutor requestExecutor;
  OAuthHttpSecurityPolicy securityPolicy;
};

struct OAuthAccessToken
{
  std::string value;
  OAuthScopeSet scopes;
};

class OAuthTokenStorage
{
public:
  OAuthTokenStorage() = default;
  OAuthTokenStorage(const OAuthTokenStorage &) = default;
  auto operator=(const OAuthTokenStorage &) -> OAuthTokenStorage & = default;
  OAuthTokenStorage(OAuthTokenStorage &&) noexcept = default;
  auto operator=(OAuthTokenStorage &&) noexcept -> OAuthTokenStorage & = default;
  virtual ~OAuthTokenStorage() = default;

  virtual auto load(std::string_view resource) const -> std::optional<OAuthAccessToken> = 0;
  virtual auto save(std::string resource, OAuthAccessToken token) -> void = 0;
};

class InMemoryOAuthTokenStorage final : public OAuthTokenStorage
{
public:
  auto load(std::string_view resource) const -> std::optional<OAuthAccessToken> override;
  auto save(std::string resource, OAuthAccessToken token) -> void override;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, OAuthAccessToken> tokensByResource_;
};

struct OAuthProtectedResourceRequest
{
  std::string method = "POST";
  std::string url;
  std::vector<OAuthHttpHeader> headers;
  std::string body;
};

using OAuthProtectedResourceRequestExecutor = std::function<OAuthHttpResponse(const OAuthProtectedResourceRequest &request)>;

struct OAuthStepUpAuthorizationRequest
{
  std::string resource;
  OAuthScopeSet requestedScopes;
  std::optional<std::string> resourceMetadataUrl;
};

using OAuthStepUpAuthorizer = std::function<OAuthAccessToken(const OAuthStepUpAuthorizationRequest &request)>;

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

auto validateAuthorizationServerPkceS256Support(const AuthorizationServerMetadata &metadata) -> void;
auto validateRedirectUriForAuthorizationCodeFlow(std::string_view redirectUri) -> void;
auto generatePkceCodePair(std::size_t verifierEntropyBytes = kDefaultPkceVerifierEntropyBytes) -> PkceCodePair;
auto buildAuthorizationUrl(const OAuthAuthorizationUrlRequest &request) -> std::string;
auto buildTokenExchangeHttpRequest(const OAuthTokenExchangeRequest &request) -> OAuthTokenHttpRequest;
auto executeTokenRequestWithPolicy(const OAuthTokenRequestExecutionRequest &request) -> OAuthHttpResponse;
auto executeProtectedResourceRequestWithStepUp(const OAuthStepUpExecutionRequest &request) -> OAuthHttpResponse;

}  // namespace mcp::auth
