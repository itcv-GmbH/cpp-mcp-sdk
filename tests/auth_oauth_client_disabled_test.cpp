#include <functional>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <mcp/auth/oauth_client.hpp>

namespace
{

auto expectAuthDisabledSecurityViolation(const std::function<void()> &entrypoint) -> void
{
  try
  {
    entrypoint();
    FAIL("Expected OAuth entrypoint to fail when MCP_SDK_ENABLE_AUTH=OFF");
  }
  catch (const mcp::auth::OAuthClientError &error)
  {
    REQUIRE(error.code() == mcp::auth::OAuthClientErrorCode::kSecurityPolicyViolation);

    const std::string message = error.what();
    REQUIRE(message.find("disabled at build time") != std::string::npos);
    REQUIRE(message.find("MCP_SDK_ENABLE_AUTH=OFF") != std::string::npos);
  }
}

auto makeAuthorizationServerMetadata() -> mcp::auth::AuthorizationServerMetadata
{
  mcp::auth::AuthorizationServerMetadata metadata;
  metadata.issuer = "https://auth.example.com";
  metadata.authorizationEndpoint = "https://auth.example.com/oauth2/authorize";
  metadata.tokenEndpoint = "https://auth.example.com/oauth2/token";
  metadata.codeChallengeMethodsSupported = {"S256"};
  return metadata;
}

}  // namespace

TEST_CASE("OAuth client entrypoints report build-time disable when auth is off", "[auth][oauth][disabled]")
{
#if MCP_SDK_ENABLE_AUTH
  SUCCEED("MCP_SDK_ENABLE_AUTH is ON; disabled behavior is validated in auth-off builds.");
#else
  const mcp::auth::AuthorizationServerMetadata metadata = makeAuthorizationServerMetadata();

  expectAuthDisabledSecurityViolation([&metadata]() { mcp::auth::validateAuthorizationServerPkceS256Support(metadata); });
  expectAuthDisabledSecurityViolation([]() { mcp::auth::validateRedirectUriForAuthorizationCodeFlow("http://localhost:9132/callback"); });
  expectAuthDisabledSecurityViolation([]() { static_cast<void>(mcp::auth::generatePkceCodePair()); });

  mcp::auth::OAuthAuthorizationUrlRequest authorizationRequest;
  authorizationRequest.authorizationServerMetadata = metadata;
  authorizationRequest.clientId = "client-id";
  authorizationRequest.redirectUri = "http://localhost:9132/callback";
  authorizationRequest.state = "state-123";
  authorizationRequest.codeChallenge = "challenge";
  authorizationRequest.resource = "https://resource.example.com/mcp";
  expectAuthDisabledSecurityViolation([&authorizationRequest]() { static_cast<void>(mcp::auth::buildAuthorizationUrl(authorizationRequest)); });

  mcp::auth::OAuthTokenExchangeRequest tokenExchangeRequest;
  tokenExchangeRequest.authorizationServerMetadata = metadata;
  tokenExchangeRequest.clientId = "client-id";
  tokenExchangeRequest.redirectUri = "http://localhost:9132/callback";
  tokenExchangeRequest.authorizationCode = "auth-code";
  tokenExchangeRequest.codeVerifier = std::string(43, 'a');
  tokenExchangeRequest.resource = "https://resource.example.com/mcp";
  expectAuthDisabledSecurityViolation([&tokenExchangeRequest]() { static_cast<void>(mcp::auth::buildTokenExchangeHttpRequest(tokenExchangeRequest)); });

  mcp::auth::OAuthTokenRequestExecutionRequest tokenExecutionRequest;
  tokenExecutionRequest.tokenRequest.url = "https://auth.example.com/oauth2/token";
  expectAuthDisabledSecurityViolation([&tokenExecutionRequest]() { static_cast<void>(mcp::auth::executeTokenRequestWithPolicy(tokenExecutionRequest)); });

  mcp::auth::OAuthStepUpExecutionRequest stepUpRequest;
  stepUpRequest.resource = "https://resource.example.com/mcp";
  expectAuthDisabledSecurityViolation([&stepUpRequest]() { static_cast<void>(mcp::auth::executeProtectedResourceRequestWithStepUp(stepUpRequest)); });
#endif
}
