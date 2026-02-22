#pragma once

#include <optional>
#include <string_view>

#include <mcp/auth/oauth_access_token.hpp>

namespace mcp::auth
{

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

}  // namespace mcp::auth
