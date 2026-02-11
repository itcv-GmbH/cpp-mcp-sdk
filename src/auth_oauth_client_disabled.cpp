#include <utility>

#include <mcp/auth/oauth_client.hpp>

namespace mcp::auth
{
namespace
{

auto authDisabledMessage() -> const char *
{
  return "OAuth client support is disabled at build time (MCP_SDK_ENABLE_AUTH=OFF).";
}

}  // namespace

OAuthClientError::OAuthClientError(OAuthClientErrorCode code, const std::string &message)
  : std::runtime_error(message)
  , code_(code)
{
}

auto OAuthClientError::code() const noexcept -> OAuthClientErrorCode
{
  return code_;
}

auto validateAuthorizationServerPkceS256Support(const AuthorizationServerMetadata &) -> void
{
  throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, authDisabledMessage());
}

auto validateRedirectUriForAuthorizationCodeFlow(std::string_view) -> void
{
  throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, authDisabledMessage());
}

auto generatePkceCodePair(std::size_t) -> PkceCodePair
{
  throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, authDisabledMessage());
}

auto buildAuthorizationUrl(const OAuthAuthorizationUrlRequest &) -> std::string
{
  throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, authDisabledMessage());
}

auto buildTokenExchangeHttpRequest(const OAuthTokenExchangeRequest &) -> OAuthTokenHttpRequest
{
  throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, authDisabledMessage());
}

auto executeTokenRequestWithPolicy(const OAuthTokenRequestExecutionRequest &) -> OAuthHttpResponse
{
  throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, authDisabledMessage());
}

auto executeProtectedResourceRequestWithStepUp(const OAuthStepUpExecutionRequest &) -> OAuthHttpResponse
{
  throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, authDisabledMessage());
}

auto InMemoryOAuthTokenStorage::load(std::string_view resource) const -> std::optional<OAuthAccessToken>
{
  const std::scoped_lock lock(mutex_);
  const auto found = tokensByResource_.find(std::string(resource));
  if (found == tokensByResource_.end())
  {
    return std::nullopt;
  }

  return found->second;
}

auto InMemoryOAuthTokenStorage::save(std::string resource, OAuthAccessToken token) -> void
{
  const std::scoped_lock lock(mutex_);
  tokensByResource_[std::move(resource)] = std::move(token);
}

}  // namespace mcp::auth
