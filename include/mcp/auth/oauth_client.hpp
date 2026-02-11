#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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

auto validateAuthorizationServerPkceS256Support(const AuthorizationServerMetadata &metadata) -> void;
auto validateRedirectUriForAuthorizationCodeFlow(std::string_view redirectUri) -> void;
auto generatePkceCodePair(std::size_t verifierEntropyBytes = kDefaultPkceVerifierEntropyBytes) -> PkceCodePair;
auto buildAuthorizationUrl(const OAuthAuthorizationUrlRequest &request) -> std::string;
auto buildTokenExchangeHttpRequest(const OAuthTokenExchangeRequest &request) -> OAuthTokenHttpRequest;

}  // namespace mcp::auth
