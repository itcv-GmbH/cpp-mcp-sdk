#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/auth/loopback_receiver.hpp>
#include <mcp/auth/oauth_client.hpp>
#include <mcp/transport/http.hpp>

namespace
{

namespace mcp_http = mcp::transport::http;

class LocalHttpServer
{
public:
  explicit LocalHttpServer(mcp::transport::HttpRequestHandler handler)
    : server_(makeServerOptions())
  {
    server_.setRequestHandler(std::move(handler));
    server_.start();
  }

  ~LocalHttpServer() { server_.stop(); }

  LocalHttpServer(const LocalHttpServer &) = delete;
  auto operator=(const LocalHttpServer &) -> LocalHttpServer & = delete;

  [[nodiscard]] auto baseUrl() const -> std::string { return std::string("http://127.0.0.1:") + std::to_string(server_.localPort()); }

private:
  static auto makeServerOptions() -> mcp::transport::HttpServerOptions
  {
    mcp::transport::HttpServerOptions options;
    options.endpoint.bindAddress = "127.0.0.1";
    options.endpoint.bindLocalhostOnly = true;
    options.endpoint.port = 0;
    return options;
  }

  mcp::transport::HttpServerRuntime server_;
};

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

auto toServerRequestMethod(std::string_view method) -> mcp_http::ServerRequestMethod
{
  if (method == "GET")
  {
    return mcp_http::ServerRequestMethod::kGet;
  }

  if (method == "DELETE")
  {
    return mcp_http::ServerRequestMethod::kDelete;
  }

  return mcp_http::ServerRequestMethod::kPost;
}

auto executeHttpRequest(const mcp::auth::OAuthTokenHttpRequest &request) -> mcp::auth::OAuthHttpResponse
{
  mcp::transport::HttpClientOptions options;
  options.endpointUrl = request.url;

  const mcp::transport::HttpClientRuntime runtime(options);

  mcp_http::ServerRequest runtimeRequest;
  runtimeRequest.method = toServerRequestMethod(request.method);
  runtimeRequest.path.clear();
  runtimeRequest.body = request.body;
  runtimeRequest.headers.reserve(request.headers.size());
  for (const mcp::auth::OAuthHttpHeader &header : request.headers)
  {
    runtimeRequest.headers.push_back(mcp_http::Header {header.name, header.value});
  }

  const mcp_http::ServerResponse runtimeResponse = runtime.execute(runtimeRequest);

  mcp::auth::OAuthHttpResponse response;
  response.statusCode = runtimeResponse.statusCode;
  response.body = runtimeResponse.body;
  response.headers.reserve(runtimeResponse.headers.size());
  for (const mcp_http::Header &header : runtimeResponse.headers)
  {
    response.headers.push_back(mcp::auth::OAuthHttpHeader {header.name, header.value});
  }

  return response;
}

auto executeProtectedResourceRequest(const mcp::auth::OAuthProtectedResourceRequest &request) -> mcp::auth::OAuthHttpResponse
{
  mcp::auth::OAuthTokenHttpRequest converted;
  converted.method = request.method;
  converted.url = request.url;
  converted.headers = request.headers;
  converted.body = request.body;
  return executeHttpRequest(converted);
}

auto bearerTokenFromHeaders(const std::vector<mcp_http::Header> &headers) -> std::optional<std::string>
{
  const auto authorization = mcp_http::getHeader(headers, mcp_http::kHeaderAuthorization);
  if (!authorization.has_value())
  {
    return std::nullopt;
  }

  const std::string_view value = *authorization;
  constexpr std::string_view kPrefix = "Bearer ";
  if (value.size() <= kPrefix.size() || value.compare(0, kPrefix.size(), kPrefix) != 0)
  {
    return std::nullopt;
  }

  return std::string(value.substr(kPrefix.size()));
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
    std::size_t finalOriginCalls = 0;

    LocalHttpServer finalOriginServer(
      [&finalOriginCalls](const mcp_http::ServerRequest &) -> mcp_http::ServerResponse
      {
        ++finalOriginCalls;
        mcp_http::ServerResponse response;
        response.statusCode = 200;
        response.body = R"({"access_token":"token"})";
        mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "application/json");
        return response;
      });

    const std::string redirectLocation = finalOriginServer.baseUrl() + "/token-final";
    LocalHttpServer firstServer(
      [redirectLocation](const mcp_http::ServerRequest &) -> mcp_http::ServerResponse
      {
        mcp_http::ServerResponse response;
        response.statusCode = 307;
        mcp_http::setHeader(response.headers, "Location", redirectLocation);
        return response;
      });

    mcp::auth::OAuthTokenRequestExecutionRequest request;
    request.tokenRequest.method = "POST";
    request.tokenRequest.url = firstServer.baseUrl() + "/token";
    request.requestExecutor = executeHttpRequest;
    request.securityPolicy.requireHttps = false;
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

    REQUIRE(finalOriginCalls == 0);
  }

  SECTION("allows redirects to different origins when explicitly configured")
  {
    std::size_t finalOriginCalls = 0;

    LocalHttpServer finalOriginServer(
      [&finalOriginCalls](const mcp_http::ServerRequest &) -> mcp_http::ServerResponse
      {
        ++finalOriginCalls;
        mcp_http::ServerResponse response;
        response.statusCode = 200;
        response.body = R"({"access_token":"token"})";
        mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "application/json");
        return response;
      });

    const std::string redirectLocation = finalOriginServer.baseUrl() + "/token-final";
    LocalHttpServer firstServer(
      [redirectLocation](const mcp_http::ServerRequest &) -> mcp_http::ServerResponse
      {
        mcp_http::ServerResponse response;
        response.statusCode = 307;
        mcp_http::setHeader(response.headers, "Location", redirectLocation);
        return response;
      });

    mcp::auth::OAuthTokenRequestExecutionRequest request;
    request.tokenRequest.method = "POST";
    request.tokenRequest.url = firstServer.baseUrl() + "/token";
    request.requestExecutor = executeHttpRequest;
    request.securityPolicy.requireHttps = false;
    request.securityPolicy.requireSameOriginRedirects = false;

    const mcp::auth::OAuthHttpResponse response = mcp::auth::executeTokenRequestWithPolicy(request);
    REQUIRE(response.statusCode == 200);
    REQUIRE(finalOriginCalls == 1);
  }
}

TEST_CASE("OAuth step-up retries on insufficient_scope and persists upgraded token", "[auth][oauth][step-up]")
{
  std::size_t requestsSeen = 0;
  std::vector<std::optional<std::string>> observedBearerTokens;

  LocalHttpServer server(
    [&requestsSeen, &observedBearerTokens](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse
    {
      const auto bearerToken = bearerTokenFromHeaders(request.headers);
      observedBearerTokens.push_back(bearerToken);

      mcp_http::ServerResponse response;
      if (requestsSeen == 0)
      {
        response.statusCode = 403;
        mcp_http::setHeader(response.headers,
                            mcp_http::kHeaderWwwAuthenticate,
                            "Bearer error=\"insufficient_scope\", scope=\"mcp:write\", "
                            "resource_metadata=\"https://resource.example/.well-known/oauth-protected-resource\"");
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
    });

  const auto tokenStorage = std::make_shared<mcp::auth::InMemoryOAuthTokenStorage>();
  tokenStorage->save("https://resource.example/mcp", mcp::auth::OAuthAccessToken {"token-initial", mcp::auth::OAuthScopeSet {{"mcp:read"}}});

  std::vector<mcp::auth::OAuthStepUpAuthorizationRequest> authorizationRequests;
  mcp::auth::OAuthStepUpExecutionRequest request;
  request.resource = "https://resource.example/mcp";
  request.protectedResourceRequest.method = "POST";
  request.protectedResourceRequest.url = server.baseUrl() + "/mcp";
  request.protectedResourceRequest.body = "{}";
  request.initialScopes = mcp::auth::OAuthScopeSet {{"mcp:read"}};
  request.tokenStorage = tokenStorage;
  request.requestExecutor = executeProtectedResourceRequest;
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

    LocalHttpServer server(
      [&requestsSeen](const mcp_http::ServerRequest &) -> mcp_http::ServerResponse
      {
        ++requestsSeen;
        mcp_http::ServerResponse response;
        response.statusCode = 403;
        mcp_http::setHeader(response.headers,
                            mcp_http::kHeaderWwwAuthenticate,
                            "Bearer error=\"insufficient_scope\", scope=\"mcp:write\", "
                            "resource_metadata=\"https://resource.example/.well-known/oauth-protected-resource\"");
        return response;
      });

    const auto tokenStorage = std::make_shared<mcp::auth::InMemoryOAuthTokenStorage>();
    tokenStorage->save("https://resource.example/mcp", mcp::auth::OAuthAccessToken {"token-initial", mcp::auth::OAuthScopeSet {{"mcp:read"}}});

    mcp::auth::OAuthStepUpExecutionRequest request;
    request.resource = "https://resource.example/mcp";
    request.protectedResourceRequest.method = "POST";
    request.protectedResourceRequest.url = server.baseUrl() + "/mcp";
    request.tokenStorage = tokenStorage;
    request.maxStepUpAttempts = 5;
    request.requestExecutor = executeProtectedResourceRequest;
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

    LocalHttpServer server(
      [&requestsSeen](const mcp_http::ServerRequest &) -> mcp_http::ServerResponse
      {
        ++requestsSeen;
        mcp_http::ServerResponse response;
        response.statusCode = 403;
        const std::string requiredScope = std::string("mcp:scope-") + std::to_string(requestsSeen);
        mcp_http::setHeader(
          response.headers,
          mcp_http::kHeaderWwwAuthenticate,
          "Bearer error=\"insufficient_scope\", scope=\"" + requiredScope + "\", resource_metadata=\"https://resource.example/.well-known/oauth-protected-resource\"");
        return response;
      });

    const auto tokenStorage = std::make_shared<mcp::auth::InMemoryOAuthTokenStorage>();
    tokenStorage->save("https://resource.example/mcp", mcp::auth::OAuthAccessToken {"token-initial", mcp::auth::OAuthScopeSet {{"mcp:read"}}});

    mcp::auth::OAuthStepUpExecutionRequest request;
    request.resource = "https://resource.example/mcp";
    request.protectedResourceRequest.method = "POST";
    request.protectedResourceRequest.url = server.baseUrl() + "/mcp";
    request.tokenStorage = tokenStorage;
    request.maxStepUpAttempts = 2;
    request.requestExecutor = executeProtectedResourceRequest;
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
