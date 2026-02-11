#include <chrono>
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <mcp/auth/loopback_receiver.hpp>
#include <mcp/auth/oauth_client.hpp>
#include <mcp/transport/http.hpp>

namespace
{

auto makeAuthorizationServerMetadata() -> mcp::auth::AuthorizationServerMetadata
{
  mcp::auth::AuthorizationServerMetadata metadata;
  metadata.issuer = "https://auth.example.com";
  metadata.authorizationEndpoint = "https://auth.example.com/oauth2/authorize";
  metadata.tokenEndpoint = "https://auth.example.com/oauth2/token";
  metadata.codeChallengeMethodsSupported = {"S256"};
  return metadata;
}

auto sendLoopbackCallbackRequest(std::uint16_t port, std::string pathAndQuery) -> mcp::transport::http::ServerResponse
{
  mcp::transport::HttpClientOptions options;
  options.endpointUrl = "http://127.0.0.1:" + std::to_string(port);

  const mcp::transport::HttpClientRuntime runtime(options);

  mcp::transport::http::ServerRequest request;
  request.method = mcp::transport::http::ServerRequestMethod::kGet;
  request.path = std::move(pathAndQuery);
  return runtime.execute(request);
}

}  // namespace

TEST_CASE("OAuth client rejects metadata without PKCE S256 support", "[auth][oauth]")
{
  mcp::auth::OAuthAuthorizationUrlRequest request;
  request.authorizationServerMetadata = makeAuthorizationServerMetadata();
  request.authorizationServerMetadata.codeChallengeMethodsSupported.clear();
  request.clientId = "my-client";
  request.redirectUri = "http://localhost:9123/callback";
  request.state = "state-123";
  request.codeChallenge = "challenge";
  request.resource = "https://mcp.example.com/mcp";

  try
  {
    static_cast<void>(mcp::auth::buildAuthorizationUrl(request));
    FAIL("Expected OAuth metadata validation failure when PKCE support is not advertised");
  }
  catch (const mcp::auth::OAuthClientError &error)
  {
    REQUIRE(error.code() == mcp::auth::OAuthClientErrorCode::kMetadataValidation);
  }
}

TEST_CASE("OAuth client enforces redirect URI policy and includes resource indicators", "[auth][oauth]")
{
  SECTION("authorization URL and token request include resource parameter")
  {
    mcp::auth::OAuthAuthorizationUrlRequest authorizationRequest;
    authorizationRequest.authorizationServerMetadata = makeAuthorizationServerMetadata();
    authorizationRequest.clientId = "my-client";
    authorizationRequest.redirectUri = "http://localhost:9123/callback";
    authorizationRequest.state = "state-123";
    authorizationRequest.codeChallenge = "challenge-value";
    authorizationRequest.resource = "https://mcp.example.com/mcp";
    authorizationRequest.scopes = mcp::auth::OAuthScopeSet {{"mcp:read"}};

    const std::string authorizationUrl = mcp::auth::buildAuthorizationUrl(authorizationRequest);
    REQUIRE(authorizationUrl.find("resource=https%3A%2F%2Fmcp.example.com%2Fmcp") != std::string::npos);

    mcp::auth::OAuthTokenExchangeRequest tokenRequest;
    tokenRequest.authorizationServerMetadata = makeAuthorizationServerMetadata();
    tokenRequest.clientId = "my-client";
    tokenRequest.redirectUri = "http://localhost:9123/callback";
    tokenRequest.authorizationCode = "auth-code-abc";
    tokenRequest.codeVerifier = std::string(43, 'a');
    tokenRequest.resource = "https://mcp.example.com/mcp";

    const mcp::auth::OAuthTokenHttpRequest builtTokenRequest = mcp::auth::buildTokenExchangeHttpRequest(tokenRequest);
    REQUIRE(builtTokenRequest.body.find("resource=https%3A%2F%2Fmcp.example.com%2Fmcp") != std::string::npos);
  }

  SECTION("http redirect URIs must use localhost")
  {
    mcp::auth::OAuthAuthorizationUrlRequest request;
    request.authorizationServerMetadata = makeAuthorizationServerMetadata();
    request.clientId = "my-client";
    request.redirectUri = "http://127.0.0.1:9123/callback";
    request.state = "state-123";
    request.codeChallenge = "challenge";
    request.resource = "https://mcp.example.com/mcp";

    try
    {
      static_cast<void>(mcp::auth::buildAuthorizationUrl(request));
      FAIL("Expected redirect URI policy enforcement failure for non-localhost HTTP redirect URI");
    }
    catch (const mcp::auth::OAuthClientError &error)
    {
      REQUIRE(error.code() == mcp::auth::OAuthClientErrorCode::kSecurityPolicyViolation);
    }
  }
}

TEST_CASE("Loopback receiver fails authorization when callback state does not match", "[auth][oauth][loopback]")
{
  mcp::auth::LoopbackReceiverOptions options;
  options.callbackPath = "/oauth/callback";
  options.authorizationTimeout = std::chrono::milliseconds(2000);

  mcp::auth::LoopbackRedirectReceiver receiver(options);
  receiver.start();

  REQUIRE(receiver.localPort() != 0);

  const auto callbackResponse = sendLoopbackCallbackRequest(receiver.localPort(), "/oauth/callback?code=abc123&state=unexpected");
  REQUIRE(callbackResponse.statusCode == 200);

  try
  {
    static_cast<void>(receiver.waitForAuthorizationCode("expected-state"));
    FAIL("Expected state mismatch error from loopback receiver");
  }
  catch (const mcp::auth::LoopbackReceiverError &error)
  {
    REQUIRE(error.code() == mcp::auth::LoopbackReceiverErrorCode::kStateMismatch);
  }
}

TEST_CASE("Loopback receiver times out when no callback is received", "[auth][oauth][loopback]")
{
  mcp::auth::LoopbackReceiverOptions options;
  options.callbackPath = "/oauth/callback";
  options.authorizationTimeout = std::chrono::milliseconds(150);

  mcp::auth::LoopbackRedirectReceiver receiver(options);
  receiver.start();

  try
  {
    static_cast<void>(receiver.waitForAuthorizationCode("expected-state"));
    FAIL("Expected timeout waiting for authorization callback");
  }
  catch (const mcp::auth::LoopbackReceiverError &error)
  {
    REQUIRE(error.code() == mcp::auth::LoopbackReceiverErrorCode::kTimeout);
  }
}
