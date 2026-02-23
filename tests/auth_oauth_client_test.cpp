#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/auth/all.hpp>
#include <mcp/auth/oauth_client.hpp>
#include <mcp/transport/all.hpp>

namespace
{

namespace mcp_http = mcp::transport::http;

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
  mcp::transport::http::HttpClientOptions options;
  options.endpointUrl = "http://127.0.0.1:" + std::to_string(port);

  const mcp::transport::http::HttpClientRuntime runtime(options);

  mcp::transport::http::ServerRequest request;
  request.method = mcp::transport::http::ServerRequestMethod::kGet;
  request.path = std::move(pathAndQuery);
  return runtime.execute(request);
}

auto bearerTokenFromOAuthHeaders(const std::vector<mcp::auth::OAuthHttpHeader> &headers) -> std::optional<std::string>
{
  for (const mcp::auth::OAuthHttpHeader &header : headers)
  {
    if (header.name != "Authorization")
    {
      continue;
    }

    constexpr std::string_view kPrefix = "Bearer ";
    if (header.value.size() <= kPrefix.size() || header.value.compare(0, kPrefix.size(), kPrefix) != 0)
    {
      return std::nullopt;
    }

    return header.value.substr(kPrefix.size());
  }

  return std::nullopt;
}

}  // namespace

TEST_CASE("OAuth client rejects metadata without PKCE S256 support", "[auth][oauth]")
{
  SECTION("code_challenge_methods_supported must be present")
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

  SECTION("code_challenge_methods_supported must include S256")
  {
    mcp::auth::OAuthTokenExchangeRequest request;
    request.authorizationServerMetadata = makeAuthorizationServerMetadata();
    request.authorizationServerMetadata.codeChallengeMethodsSupported = {"plain"};
    request.clientId = "my-client";
    request.redirectUri = "http://localhost:9123/callback";
    request.authorizationCode = "auth-code";
    request.codeVerifier = std::string(43, 'a');
    request.resource = "https://mcp.example.com/mcp";

    try
    {
      static_cast<void>(mcp::auth::buildTokenExchangeHttpRequest(request));
      FAIL("Expected OAuth metadata validation failure when PKCE S256 is missing");
    }
    catch (const mcp::auth::OAuthClientError &error)
    {
      REQUIRE(error.code() == mcp::auth::OAuthClientErrorCode::kMetadataValidation);
    }
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

  SECTION("redirect URIs reject ASCII control characters")
  {
    mcp::auth::OAuthAuthorizationUrlRequest request;
    request.authorizationServerMetadata = makeAuthorizationServerMetadata();
    request.clientId = "my-client";
    request.redirectUri = "http://localhost:9123/call\nback";
    request.state = "state-123";
    request.codeChallenge = "challenge";
    request.resource = "https://mcp.example.com/mcp";

    try
    {
      static_cast<void>(mcp::auth::buildAuthorizationUrl(request));
      FAIL("Expected redirect URI validation failure for ASCII control characters");
    }
    catch (const mcp::auth::OAuthClientError &error)
    {
      REQUIRE(error.code() == mcp::auth::OAuthClientErrorCode::kInvalidInput);
    }
  }
}

TEST_CASE("OAuth token request execution enforces redirect hardening", "[auth][oauth][security]")
{
  SECTION("rejects non-HTTPS token endpoint URLs by default")
  {
    bool executorCalled = false;

    mcp::auth::OAuthTokenRequestExecutionRequest request;
    request.tokenRequest.method = "POST";
    request.tokenRequest.url = "http://127.0.0.1:65535/token";
    request.requestExecutor = [&executorCalled](const mcp::auth::OAuthTokenHttpRequest &) -> mcp::auth::OAuthHttpResponse
    {
      executorCalled = true;
      return {};
    };

    try
    {
      static_cast<void>(mcp::auth::executeTokenRequestWithPolicy(request));
      FAIL("Expected token endpoint policy failure for non-HTTPS URL");
    }
    catch (const mcp::auth::OAuthClientError &error)
    {
      REQUIRE(error.code() == mcp::auth::OAuthClientErrorCode::kSecurityPolicyViolation);
    }

    REQUIRE_FALSE(executorCalled);
  }

  SECTION("blocks redirects to different origins unless explicitly allowed")
  {
    std::size_t redirectedEndpointCalls = 0;

    mcp::auth::OAuthTokenRequestExecutionRequest request;
    request.tokenRequest.method = "POST";
    request.tokenRequest.url = "https://auth.example.com/token";
    request.requestExecutor = [&redirectedEndpointCalls](const mcp::auth::OAuthTokenHttpRequest &tokenRequest) -> mcp::auth::OAuthHttpResponse
    {
      mcp::auth::OAuthHttpResponse response;
      if (tokenRequest.url == "https://auth.example.com/token")
      {
        response.statusCode = 307;
        response.headers.push_back({"Location", "https://unexpected.example.net/token"});
        return response;
      }

      ++redirectedEndpointCalls;
      response.statusCode = 200;
      response.body = R"({"access_token":"token"})";
      return response;
    };
    request.securityPolicy.requireSameOriginRedirects = true;

    try
    {
      static_cast<void>(mcp::auth::executeTokenRequestWithPolicy(request));
      FAIL("Expected token endpoint redirect origin policy failure");
    }
    catch (const mcp::auth::OAuthClientError &error)
    {
      REQUIRE(error.code() == mcp::auth::OAuthClientErrorCode::kSecurityPolicyViolation);
    }

    REQUIRE(redirectedEndpointCalls == 0);
  }

  SECTION("rejects HTTPS redirect downgrades to HTTP")
  {
    std::size_t redirectedEndpointCalls = 0;

    mcp::auth::OAuthTokenRequestExecutionRequest request;
    request.tokenRequest.method = "POST";
    request.tokenRequest.url = "https://auth.example.com/token";
    request.requestExecutor = [&redirectedEndpointCalls](const mcp::auth::OAuthTokenHttpRequest &tokenRequest) -> mcp::auth::OAuthHttpResponse
    {
      mcp::auth::OAuthHttpResponse response;
      if (tokenRequest.url == "https://auth.example.com/token")
      {
        response.statusCode = 307;
        response.headers.push_back({"Location", "http://auth.example.com/token"});
        return response;
      }

      ++redirectedEndpointCalls;
      response.statusCode = 200;
      response.body = R"({"access_token":"token"})";
      return response;
    };

    try
    {
      static_cast<void>(mcp::auth::executeTokenRequestWithPolicy(request));
      FAIL("Expected token endpoint redirect downgrade policy failure");
    }
    catch (const mcp::auth::OAuthClientError &error)
    {
      REQUIRE(error.code() == mcp::auth::OAuthClientErrorCode::kSecurityPolicyViolation);
    }

    REQUIRE(redirectedEndpointCalls == 0);
  }

  SECTION("allows redirects to different origins when explicitly configured")
  {
    std::size_t redirectedEndpointCalls = 0;

    mcp::auth::OAuthTokenRequestExecutionRequest request;
    request.tokenRequest.method = "POST";
    request.tokenRequest.url = "https://auth.example.com/token";
    request.requestExecutor = [&redirectedEndpointCalls](const mcp::auth::OAuthTokenHttpRequest &tokenRequest) -> mcp::auth::OAuthHttpResponse
    {
      mcp::auth::OAuthHttpResponse response;
      if (tokenRequest.url == "https://auth.example.com/token")
      {
        response.statusCode = 307;
        response.headers.push_back({"Location", "https://edge-auth.example.com/token"});
        return response;
      }

      ++redirectedEndpointCalls;
      response.statusCode = 200;
      response.body = R"({"access_token":"token"})";
      return response;
    };
    request.securityPolicy.requireSameOriginRedirects = false;

    const mcp::auth::OAuthHttpResponse response = mcp::auth::executeTokenRequestWithPolicy(request);
    REQUIRE(response.statusCode == 200);
    REQUIRE(redirectedEndpointCalls == 1);
  }
}

TEST_CASE("OAuth step-up retries on insufficient_scope and persists upgraded token", "[auth][oauth][step-up]")
{
  std::size_t requestsSeen = 0;
  std::vector<std::optional<std::string>> observedBearerTokens;

  const auto tokenStorage = std::make_shared<mcp::auth::InMemoryOAuthTokenStorage>();
  tokenStorage->save("https://resource.example/mcp", mcp::auth::OAuthAccessToken {"token-initial", mcp::auth::OAuthScopeSet {{"mcp:read"}}});

  std::vector<mcp::auth::OAuthStepUpAuthorizationRequest> authorizationRequests;
  mcp::auth::OAuthStepUpExecutionRequest request;
  request.resource = "https://resource.example/mcp";
  request.protectedResourceRequest.method = "POST";
  request.protectedResourceRequest.url = "https://resource.example/mcp";
  request.protectedResourceRequest.body = "{}";
  request.initialScopes = mcp::auth::OAuthScopeSet {{"mcp:read"}};
  request.tokenStorage = tokenStorage;
  request.requestExecutor = [&requestsSeen, &observedBearerTokens](const mcp::auth::OAuthProtectedResourceRequest &protectedResourceRequest) -> mcp::auth::OAuthHttpResponse
  {
    const auto bearerToken = bearerTokenFromOAuthHeaders(protectedResourceRequest.headers);
    observedBearerTokens.push_back(bearerToken);

    mcp::auth::OAuthHttpResponse response;
    if (requestsSeen == 0)
    {
      response.statusCode = 403;
      response.headers.push_back({
        "WWW-Authenticate",
        "Bearer error=\"insufficient_scope\", scope=\"mcp:write\", "
        "resource_metadata=\"https://resource.example/.well-known/oauth-protected-resource\"",
      });
    }
    else
    {
      if (bearerToken == std::optional<std::string> {"token-step-up"})
      {
        response.statusCode = 200;
        response.body = "ok";
      }
      else
      {
        response.statusCode = 401;
      }
    }

    ++requestsSeen;
    return response;
  };
  request.authorizer = [&authorizationRequests](const mcp::auth::OAuthStepUpAuthorizationRequest &authorizationRequest) -> mcp::auth::OAuthAccessToken
  {
    authorizationRequests.push_back(authorizationRequest);

    mcp::auth::OAuthAccessToken token;
    token.value = "token-step-up";
    token.scopes = authorizationRequest.requestedScopes;
    return token;
  };

  const mcp::auth::OAuthHttpResponse response = mcp::auth::executeProtectedResourceRequestWithStepUp(request);
  REQUIRE(response.statusCode == 200);
  REQUIRE(requestsSeen == 2);
  REQUIRE(observedBearerTokens.size() == 2);
  REQUIRE(observedBearerTokens[0] == std::optional<std::string> {"token-initial"});
  REQUIRE(observedBearerTokens[1] == std::optional<std::string> {"token-step-up"});
  REQUIRE(authorizationRequests.size() == 1);
  REQUIRE(authorizationRequests.front().requestedScopes.values == std::vector<std::string> {"mcp:read", "mcp:write"});
  REQUIRE(authorizationRequests.front().resourceMetadataUrl == std::optional<std::string> {"https://resource.example/.well-known/oauth-protected-resource"});

  const auto savedToken = tokenStorage->load("https://resource.example/mcp");
  REQUIRE(savedToken.has_value());
  REQUIRE(savedToken->value == "token-step-up");
}

TEST_CASE("OAuth step-up prevents retry loops and enforces retry cap", "[auth][oauth][step-up]")
{
  SECTION("stops when the same scope set is challenged repeatedly")
  {
    std::size_t requestsSeen = 0;
    std::size_t authorizationCalls = 0;

    const auto tokenStorage = std::make_shared<mcp::auth::InMemoryOAuthTokenStorage>();
    tokenStorage->save("https://resource.example/mcp", mcp::auth::OAuthAccessToken {"token-initial", mcp::auth::OAuthScopeSet {{"mcp:read"}}});

    mcp::auth::OAuthStepUpExecutionRequest request;
    request.resource = "https://resource.example/mcp";
    request.protectedResourceRequest.method = "POST";
    request.protectedResourceRequest.url = "https://resource.example/mcp";
    request.tokenStorage = tokenStorage;
    request.maxStepUpAttempts = 5;
    request.requestExecutor = [&requestsSeen](const mcp::auth::OAuthProtectedResourceRequest &) -> mcp::auth::OAuthHttpResponse
    {
      ++requestsSeen;

      mcp::auth::OAuthHttpResponse response;
      response.statusCode = 403;
      response.headers.push_back({
        "WWW-Authenticate",
        "Bearer error=\"insufficient_scope\", scope=\"mcp:write\", "
        "resource_metadata=\"https://resource.example/.well-known/oauth-protected-resource\"",
      });
      return response;
    };
    request.authorizer = [&authorizationCalls](const mcp::auth::OAuthStepUpAuthorizationRequest &authorizationRequest) -> mcp::auth::OAuthAccessToken
    {
      ++authorizationCalls;
      return mcp::auth::OAuthAccessToken {
        "token-step-up-" + std::to_string(authorizationCalls),
        authorizationRequest.requestedScopes,
      };
    };

    const mcp::auth::OAuthHttpResponse response = mcp::auth::executeProtectedResourceRequestWithStepUp(request);
    REQUIRE(response.statusCode == 403);
    REQUIRE(authorizationCalls == 1);
    REQUIRE(requestsSeen == 2);
  }

  SECTION("honors maxStepUpAttempts when required scopes keep changing")
  {
    std::size_t requestsSeen = 0;
    std::size_t authorizationCalls = 0;

    const auto tokenStorage = std::make_shared<mcp::auth::InMemoryOAuthTokenStorage>();
    tokenStorage->save("https://resource.example/mcp", mcp::auth::OAuthAccessToken {"token-initial", mcp::auth::OAuthScopeSet {{"mcp:read"}}});

    mcp::auth::OAuthStepUpExecutionRequest request;
    request.resource = "https://resource.example/mcp";
    request.protectedResourceRequest.method = "POST";
    request.protectedResourceRequest.url = "https://resource.example/mcp";
    request.tokenStorage = tokenStorage;
    request.maxStepUpAttempts = 2;
    request.requestExecutor = [&requestsSeen](const mcp::auth::OAuthProtectedResourceRequest &) -> mcp::auth::OAuthHttpResponse
    {
      ++requestsSeen;

      mcp::auth::OAuthHttpResponse response;
      response.statusCode = 403;

      const std::string requiredScope = std::string("mcp:scope-") + std::to_string(requestsSeen);
      response.headers.push_back({
        "WWW-Authenticate",
        "Bearer error=\"insufficient_scope\", scope=\"" + requiredScope + "\", resource_metadata=\"https://resource.example/.well-known/oauth-protected-resource\"",
      });
      return response;
    };
    request.authorizer = [&authorizationCalls](const mcp::auth::OAuthStepUpAuthorizationRequest &authorizationRequest) -> mcp::auth::OAuthAccessToken
    {
      ++authorizationCalls;
      return mcp::auth::OAuthAccessToken {
        "token-step-up-" + std::to_string(authorizationCalls),
        authorizationRequest.requestedScopes,
      };
    };

    const mcp::auth::OAuthHttpResponse response = mcp::auth::executeProtectedResourceRequestWithStepUp(request);
    REQUIRE(response.statusCode == 403);
    REQUIRE(authorizationCalls == 2);
    REQUIRE(requestsSeen == 3);
  }
}

TEST_CASE("OAuth token storage tracks scopes per resource and overwrites entries", "[auth][oauth][storage]")
{
  mcp::auth::InMemoryOAuthTokenStorage storage;

  storage.save("https://resource.example.com/mcp", mcp::auth::OAuthAccessToken {"token-resource-a", mcp::auth::OAuthScopeSet {{"mcp:read"}}});
  storage.save("https://other-resource.example.com/mcp", mcp::auth::OAuthAccessToken {"token-resource-b", mcp::auth::OAuthScopeSet {{"mcp:admin"}}});

  const auto resourceAToken = storage.load("https://resource.example.com/mcp");
  const auto resourceBToken = storage.load("https://other-resource.example.com/mcp");

  REQUIRE(resourceAToken.has_value());
  REQUIRE(resourceAToken->value == "token-resource-a");
  REQUIRE(resourceAToken->scopes.values == std::vector<std::string> {"mcp:read"});

  REQUIRE(resourceBToken.has_value());
  REQUIRE(resourceBToken->value == "token-resource-b");
  REQUIRE(resourceBToken->scopes.values == std::vector<std::string> {"mcp:admin"});

  storage.save("https://resource.example.com/mcp", mcp::auth::OAuthAccessToken {"token-resource-a-updated", mcp::auth::OAuthScopeSet {{"mcp:read", "mcp:write"}}});

  const auto updatedResourceAToken = storage.load("https://resource.example.com/mcp");
  const auto unchangedResourceBToken = storage.load("https://other-resource.example.com/mcp");

  REQUIRE(updatedResourceAToken.has_value());
  REQUIRE(updatedResourceAToken->value == "token-resource-a-updated");
  REQUIRE(updatedResourceAToken->scopes.values == std::vector<std::string> {"mcp:read", "mcp:write"});

  REQUIRE(unchangedResourceBToken.has_value());
  REQUIRE(unchangedResourceBToken->value == "token-resource-b");
  REQUIRE(unchangedResourceBToken->scopes.values == std::vector<std::string> {"mcp:admin"});
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
