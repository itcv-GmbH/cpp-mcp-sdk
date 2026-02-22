#pragma once

#include <mcp/auth/oauth_token_verification_request.hpp>
#include <mcp/auth/oauth_token_verification_result.hpp>

namespace mcp::auth
{

class OAuthTokenVerifier
{
public:
  OAuthTokenVerifier() = default;
  OAuthTokenVerifier(const OAuthTokenVerifier &) = delete;
  OAuthTokenVerifier(OAuthTokenVerifier &&) = delete;
  auto operator=(const OAuthTokenVerifier &) -> OAuthTokenVerifier & = delete;
  auto operator=(OAuthTokenVerifier &&) -> OAuthTokenVerifier & = delete;
  virtual ~OAuthTokenVerifier() = default;
  virtual auto verifyToken(const OAuthTokenVerificationRequest &request) const -> OAuthTokenVerificationResult = 0;
};

}  // namespace mcp::auth
