#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/auth/all.hpp>
#include <mcp/auth/oauth_client.hpp>
#include <mcp/auth/all.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/transport/all.hpp>
#include <openssl/evp.h>
#include <openssl/sha.h>

namespace
{

namespace mcp_http = mcp::transport::http;

class InvalidTokenVerifier final : public mcp::auth::OAuthTokenVerifier
{
public:
  auto verifyToken(const mcp::auth::OAuthTokenVerificationRequest &) const -> mcp::auth::OAuthTokenVerificationResult override { return {}; }
};

class MockOAuthEndpoints
{
public:
  MockOAuthEndpoints(std::string resourceOrigin, std::string authorizationOrigin)
    : resourceOrigin_(std::move(resourceOrigin))
    , authorizationOrigin_(std::move(authorizationOrigin))
  {
  }

  auto addResourceResponse(std::string path, mcp::auth::DiscoveryHttpResponse response) -> void
  {
    responsesByUrl_[resourceOrigin_ + normalizePath(std::move(path))].push_back(std::move(response));
  }

  auto addAuthorizationResponse(std::string path, mcp::auth::DiscoveryHttpResponse response) -> void
  {
    responsesByUrl_[authorizationOrigin_ + normalizePath(std::move(path))].push_back(std::move(response));
  }

  auto fetch(const mcp::auth::DiscoveryHttpRequest &request) -> mcp::auth::DiscoveryHttpResponse
  {
    requestedUrls_.push_back(request.url);

    auto queue = responsesByUrl_.find(request.url);
    if (queue == responsesByUrl_.end() || queue->second.empty())
    {
      throw std::runtime_error("No mock endpoint response for URL: " + request.url);
    }

    mcp::auth::DiscoveryHttpResponse response = queue->second.front();
    queue->second.erase(queue->second.begin());
    return response;
  }

  [[nodiscard]] auto requestedUrls() const -> const std::vector<std::string> & { return requestedUrls_; }
  [[nodiscard]] auto resourceOrigin() const -> const std::string & { return resourceOrigin_; }
  [[nodiscard]] auto authorizationOrigin() const -> const std::string & { return authorizationOrigin_; }

private:
  static auto normalizePath(std::string path) -> std::string
  {
    if (path.empty())
    {
      return "/";
    }

    if (path.front() != '/')
    {
      path.insert(path.begin(), '/');
    }

    return path;
  }

  std::string resourceOrigin_;
  std::string authorizationOrigin_;
  std::unordered_map<std::string, std::vector<mcp::auth::DiscoveryHttpResponse>> responsesByUrl_;
  std::vector<std::string> requestedUrls_;
};

auto makeJsonResponse(std::uint16_t statusCode, std::string body) -> mcp::auth::DiscoveryHttpResponse
{
  mcp::auth::DiscoveryHttpResponse response;
  response.statusCode = statusCode;
  response.body = std::move(body);
  response.headers.push_back({"Content-Type", "application/json"});
  return response;
}

auto makeRedirectResponse(std::string location) -> mcp::auth::DiscoveryHttpResponse
{
  mcp::auth::DiscoveryHttpResponse response;
  response.statusCode = 302;
  response.headers.push_back({"Location", std::move(location)});
  return response;
}

auto makeAuthorizationServerMetadata() -> mcp::auth::AuthorizationServerMetadata
{
  mcp::auth::AuthorizationServerMetadata metadata;
  metadata.issuer = "https://auth.example.com";
  metadata.authorizationEndpoint = "https://auth.example.com/authorize";
  metadata.tokenEndpoint = "https://auth.example.com/token";
  metadata.codeChallengeMethodsSupported = {"S256"};
  return metadata;
}

auto makeRequestBody(std::int64_t id, std::string method) -> std::string
{
  mcp::jsonrpc::Request request;
  request.id = id;
  request.method = std::move(method);
  request.params = mcp::jsonrpc::JsonValue::object();
  return mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {request});
}

auto makeDynamicRegistrationSuccessResponse(std::string clientId, std::vector<std::string> redirectUris, std::string tokenEndpointAuthMethod, std::string clientSecret = "")
  -> mcp::auth::ClientRegistrationHttpResponse
{
  mcp::jsonrpc::JsonValue responseJson = mcp::jsonrpc::JsonValue::object();
  responseJson["client_id"] = std::move(clientId);
  responseJson["redirect_uris"] = mcp::jsonrpc::JsonValue::array();
  for (const std::string &redirectUri : redirectUris)
  {
    responseJson["redirect_uris"].push_back(redirectUri);
  }

  responseJson["token_endpoint_auth_method"] = std::move(tokenEndpointAuthMethod);
  if (!clientSecret.empty())
  {
    responseJson["client_secret"] = std::move(clientSecret);
  }

  std::string encodedResponse;
  responseJson.dump(encodedResponse);

  mcp::auth::ClientRegistrationHttpResponse response;
  response.statusCode = 201;
  response.body = std::move(encodedResponse);
  response.headers.push_back({"Content-Type", "application/json"});
  return response;
}

auto toLowerAscii(std::string_view value) -> std::string
{
  std::string lowered;
  lowered.reserve(value.size());
  for (const char character : value)
  {
    lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }

  return lowered;
}

auto oauthHeaderValue(const std::vector<mcp::auth::OAuthHttpHeader> &headers, std::string_view name) -> std::optional<std::string>
{
  const std::string loweredName = toLowerAscii(name);
  for (const mcp::auth::OAuthHttpHeader &header : headers)
  {
    if (toLowerAscii(header.name) == loweredName)
    {
      return header.value;
    }
  }

  return std::nullopt;
}

auto base64UrlEncode(const std::vector<std::uint8_t> &input) -> std::string
{
  static constexpr char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string encoded;
  encoded.reserve(((input.size() + 2U) / 3U) * 4U);

  std::size_t index = 0;
  while (index + 2U < input.size())
  {
    const std::uint32_t triple =
      (static_cast<std::uint32_t>(input[index]) << 16U) | (static_cast<std::uint32_t>(input[index + 1U]) << 8U) | static_cast<std::uint32_t>(input[index + 2U]);

    encoded.push_back(kBase64Alphabet[(triple >> 18U) & 0x3FU]);
    encoded.push_back(kBase64Alphabet[(triple >> 12U) & 0x3FU]);
    encoded.push_back(kBase64Alphabet[(triple >> 6U) & 0x3FU]);
    encoded.push_back(kBase64Alphabet[triple & 0x3FU]);
    index += 3U;
  }

  const std::size_t remainder = input.size() - index;
  if (remainder == 1U)
  {
    const std::uint32_t triple = static_cast<std::uint32_t>(input[index]) << 16U;
    encoded.push_back(kBase64Alphabet[(triple >> 18U) & 0x3FU]);
    encoded.push_back(kBase64Alphabet[(triple >> 12U) & 0x3FU]);
    encoded.push_back('=');
    encoded.push_back('=');
  }
  else if (remainder == 2U)
  {
    const std::uint32_t triple = (static_cast<std::uint32_t>(input[index]) << 16U) | (static_cast<std::uint32_t>(input[index + 1U]) << 8U);
    encoded.push_back(kBase64Alphabet[(triple >> 18U) & 0x3FU]);
    encoded.push_back(kBase64Alphabet[(triple >> 12U) & 0x3FU]);
    encoded.push_back(kBase64Alphabet[(triple >> 6U) & 0x3FU]);
    encoded.push_back('=');
  }

  std::replace(encoded.begin(), encoded.end(), '+', '-');
  std::replace(encoded.begin(), encoded.end(), '/', '_');

  while (!encoded.empty() && encoded.back() == '=')
  {
    encoded.pop_back();
  }

  return encoded;
}

auto sha256Digest(std::string_view value) -> std::vector<std::uint8_t>
{
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest {};
  unsigned int digestLength = 0;
  const int success = EVP_Digest(value.data(), value.size(), digest.data(), &digestLength, EVP_sha256(), nullptr);
  REQUIRE(success == 1);
  return std::vector<std::uint8_t>(digest.begin(), digest.begin() + digestLength);
}

auto makeInsufficientScopeResponse(std::string requiredScope) -> mcp::auth::OAuthHttpResponse
{
  mcp::auth::OAuthHttpResponse response;
  response.statusCode = 403;
  response.headers.push_back(
    {"WWW-Authenticate",
     "Bearer error=\"insufficient_scope\", scope=\"" + std::move(requiredScope) + "\", resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource\""});
  return response;
}

}  // namespace

TEST_CASE("Authorization 401 challenge includes RFC9728 resource_metadata", "[conformance][authorization]")
{
  const auto verifier = std::make_shared<InvalidTokenVerifier>();

  mcp_http::StreamableHttpServerOptions options;
  options.http.authorization = mcp::auth::OAuthServerAuthorizationOptions {};
  options.http.authorization->tokenVerifier = verifier;
  options.http.authorization->protectedResourceMetadata.resource = "https://mcp.example.com/mcp";
  options.http.authorization->protectedResourceMetadata.authorizationServers = {"https://auth.example.com"};
  options.http.authorization->defaultRequiredScopes.values = {"mcp:read"};

  mcp_http::StreamableHttpServer server(options);

  mcp_http::ServerRequest request;
  request.method = mcp_http::ServerRequestMethod::kPost;
  request.path = "/mcp";
  request.body = makeRequestBody(1, "ping");
  mcp_http::setHeader(request.headers, mcp_http::kHeaderContentType, "application/json");

  const mcp_http::ServerResponse response = server.handleRequest(request);
  REQUIRE(response.statusCode == 401);

  const auto challenge = mcp_http::getHeader(response.headers, mcp_http::kHeaderWwwAuthenticate);
  REQUIRE(challenge.has_value());
  REQUIRE(challenge->find("Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\"") != std::string::npos);
  REQUIRE(challenge->find("scope=\"mcp:read\"") != std::string::npos);
}

TEST_CASE("Authorization discovery honors RFC9728 fallback and RFC8414/OIDC probe order", "[conformance][authorization]")
{
  MockOAuthEndpoints endpoints("https://mcp.example.com", "https://auth.example.com");

  endpoints.addResourceResponse("/.well-known/oauth-protected-resource/public/mcp", makeJsonResponse(404, ""));
  endpoints.addResourceResponse("/.well-known/oauth-protected-resource",
                                makeJsonResponse(200,
                                                 R"({
                        "resource": "https://mcp.example.com/public/mcp",
                        "authorization_servers": ["https://auth.example.com/tenant1"],
                        "scopes_supported": ["mcp:read", "mcp:write"]
                      })"));

  endpoints.addAuthorizationResponse("/.well-known/oauth-authorization-server/tenant1", makeJsonResponse(404, ""));
  endpoints.addAuthorizationResponse("/.well-known/openid-configuration/tenant1",
                                     makeJsonResponse(200,
                                                      R"({
                        "issuer": "https://auth.example.com/tenant1",
                        "authorization_endpoint": "https://auth.example.com/tenant1/oauth2/authorize",
                        "token_endpoint": "https://auth.example.com/tenant1/oauth2/token",
                        "code_challenge_methods_supported": ["S256"]
                      })"));

  mcp::auth::AuthorizationDiscoveryRequest request;
  request.mcpEndpointUrl = "https://mcp.example.com/public/mcp";
  request.wwwAuthenticateHeaderValues = {
    "Bearer realm=\"mcp\"",
  };
  request.httpFetcher = [&endpoints](const mcp::auth::DiscoveryHttpRequest &httpRequest) -> mcp::auth::DiscoveryHttpResponse { return endpoints.fetch(httpRequest); };
  request.dnsResolver = [](std::string_view) -> std::vector<std::string> { return {"93.184.216.34"}; };

  const mcp::auth::AuthorizationDiscoveryResult result = mcp::auth::discoverAuthorizationMetadata(request);

  REQUIRE(result.protectedResourceMetadataUrl == "https://mcp.example.com/.well-known/oauth-protected-resource");
  REQUIRE(result.authorizationServerMetadataUrl == "https://auth.example.com/.well-known/openid-configuration/tenant1");
  REQUIRE(result.authorizationServerMetadata.authorizationEndpoint.has_value());
  REQUIRE(result.authorizationServerMetadata.tokenEndpoint.has_value());
  REQUIRE(result.selectedScopes.has_value());
  REQUIRE(result.selectedScopes->values == std::vector<std::string> {"mcp:read", "mcp:write"});

  REQUIRE(endpoints.requestedUrls()
          == std::vector<std::string> {
            "https://mcp.example.com/.well-known/oauth-protected-resource/public/mcp",
            "https://mcp.example.com/.well-known/oauth-protected-resource",
            "https://auth.example.com/.well-known/oauth-authorization-server/tenant1",
            "https://auth.example.com/.well-known/openid-configuration/tenant1",
          });
}

TEST_CASE("PKCE metadata verification requires code_challenge_methods_supported and S256", "[conformance][authorization]")
{
  mcp::auth::AuthorizationServerMetadata metadata = makeAuthorizationServerMetadata();

  SECTION("fails when code_challenge_methods_supported is absent")
  {
    metadata.codeChallengeMethodsSupported.clear();

    try
    {
      mcp::auth::validateAuthorizationServerPkceS256Support(metadata);
      FAIL("Expected metadata validation failure when code_challenge_methods_supported is missing");
    }
    catch (const mcp::auth::OAuthClientError &error)
    {
      REQUIRE(error.code() == mcp::auth::OAuthClientErrorCode::kMetadataValidation);
    }
  }

  SECTION("fails when S256 is not advertised")
  {
    metadata.codeChallengeMethodsSupported = {"plain"};

    try
    {
      mcp::auth::validateAuthorizationServerPkceS256Support(metadata);
      FAIL("Expected metadata validation failure when S256 is unavailable");
    }
    catch (const mcp::auth::OAuthClientError &error)
    {
      REQUIRE(error.code() == mcp::auth::OAuthClientErrorCode::kMetadataValidation);
    }
  }

  SECTION("accepts advertised S256 support")
  {
    metadata.codeChallengeMethodsSupported = {"plain", "S256"};
    REQUIRE_NOTHROW(mcp::auth::validateAuthorizationServerPkceS256Support(metadata));
  }
}

TEST_CASE("PKCE S256 generation computes correct challenge", "[conformance][authorization]")
{
  const mcp::auth::PkceCodePair pair = mcp::auth::generatePkceCodePair();

  REQUIRE(pair.codeChallengeMethod == "S256");
  REQUIRE(pair.codeVerifier.size() >= 43);
  REQUIRE(pair.codeVerifier.size() <= 128);

  const std::vector<std::uint8_t> digest = sha256Digest(pair.codeVerifier);
  const std::string expectedChallenge = base64UrlEncode(digest);
  REQUIRE(pair.codeChallenge == expectedChallenge);
}

TEST_CASE("OAuth authorization and token requests include resource indicators", "[conformance][authorization]")
{
  const mcp::auth::AuthorizationServerMetadata metadata = makeAuthorizationServerMetadata();
  constexpr std::string_view kResource = "https://mcp.example.com/mcp";

  mcp::auth::OAuthAuthorizationUrlRequest authorizationRequest;
  authorizationRequest.authorizationServerMetadata = metadata;
  authorizationRequest.clientId = "sdk-client";
  authorizationRequest.redirectUri = "http://localhost:8721/callback";
  authorizationRequest.state = "state-123";
  authorizationRequest.codeChallenge = "challenge-value";
  authorizationRequest.resource = std::string(kResource);
  authorizationRequest.scopes = mcp::auth::OAuthScopeSet {{"mcp:read"}};

  const std::string authorizationUrl = mcp::auth::buildAuthorizationUrl(authorizationRequest);
  REQUIRE(authorizationUrl.find("resource=https%3A%2F%2Fmcp.example.com%2Fmcp") != std::string::npos);

  mcp::auth::OAuthTokenExchangeRequest tokenRequest;
  tokenRequest.authorizationServerMetadata = metadata;
  tokenRequest.clientId = "sdk-client";
  tokenRequest.redirectUri = "http://localhost:8721/callback";
  tokenRequest.authorizationCode = "auth-code";
  tokenRequest.codeVerifier = std::string(43, 'a');
  tokenRequest.resource = std::string(kResource);

  const mcp::auth::OAuthTokenHttpRequest tokenHttpRequest = mcp::auth::buildTokenExchangeHttpRequest(tokenRequest);
  REQUIRE(tokenHttpRequest.body.find("resource=https%3A%2F%2Fmcp.example.com%2Fmcp") != std::string::npos);
}

TEST_CASE("Client registration strategy selection follows pre-reg then CIMD then dynamic", "[conformance][authorization]")
{
  mcp::auth::ResolveClientRegistrationRequest request;
  request.authorizationServerMetadata.issuer = "https://auth.example.com";
  request.authorizationServerMetadata.registrationEndpoint = "https://auth.example.com/register";
  request.authorizationServerMetadata.clientIdMetadataDocumentSupported = true;
  request.strategyConfiguration.preRegistered = mcp::auth::PreRegisteredClientConfiguration {
    "pre-reg-client",
    {"http://127.0.0.1:8721/callback"},
    mcp::auth::ClientAuthenticationMethod::kNone,
    std::nullopt,
  };
  request.strategyConfiguration.clientIdMetadataDocument = mcp::auth::ClientIdMetadataDocumentConfiguration {
    "https://client.example.com/oauth/client-metadata.json",
    "Conformance Client",
    {"http://127.0.0.1:8721/callback"},
    mcp::auth::ClientAuthenticationMethod::kNone,
    std::nullopt,
    std::nullopt,
  };
  request.strategyConfiguration.dynamic.enabled = true;
  request.strategyConfiguration.dynamic.clientName = "Dynamic Client";
  request.strategyConfiguration.dynamic.redirectUris = {"http://127.0.0.1:8721/callback"};

  std::uint32_t dynamicCalls = 0;
  request.httpExecutor = [&dynamicCalls](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
  {
    ++dynamicCalls;
    return makeDynamicRegistrationSuccessResponse("dynamic-client", {"http://127.0.0.1:8721/callback"}, "none");
  };

  SECTION("pre-registered client wins when available")
  {
    const mcp::auth::ClientRegistrationResult result = mcp::auth::resolveClientRegistration(request);
    REQUIRE(result.strategy == mcp::auth::ClientRegistrationStrategy::kPreRegistered);
    REQUIRE(result.client.clientId == "pre-reg-client");
    REQUIRE(dynamicCalls == 0);
  }

  SECTION("CIMD is selected when pre-registration is unavailable")
  {
    request.strategyConfiguration.preRegistered.reset();

    const mcp::auth::ClientRegistrationResult result = mcp::auth::resolveClientRegistration(request);
    REQUIRE(result.strategy == mcp::auth::ClientRegistrationStrategy::kClientIdMetadataDocument);
    REQUIRE(result.client.clientId == "https://client.example.com/oauth/client-metadata.json");
    REQUIRE(dynamicCalls == 0);
  }

  SECTION("dynamic registration is selected when pre-reg and CIMD are unavailable")
  {
    request.strategyConfiguration.preRegistered.reset();
    request.strategyConfiguration.clientIdMetadataDocument.reset();
    request.authorizationServerMetadata.clientIdMetadataDocumentSupported = false;

    const mcp::auth::ClientRegistrationResult result = mcp::auth::resolveClientRegistration(request);
    REQUIRE(result.strategy == mcp::auth::ClientRegistrationStrategy::kDynamic);
    REQUIRE(result.client.clientId == "dynamic-client");
    REQUIRE(dynamicCalls == 1);
  }
}

TEST_CASE("Step-up retry logic enforces loop prevention and retry caps", "[conformance][authorization]")
{
  SECTION("repeated scope set is attempted once")
  {
    std::size_t requestsSeen = 0;
    std::size_t authorizationCalls = 0;

    const auto tokenStorage = std::make_shared<mcp::auth::InMemoryOAuthTokenStorage>();
    tokenStorage->save("https://mcp.example.com/mcp", mcp::auth::OAuthAccessToken {"token-initial", mcp::auth::OAuthScopeSet {{"mcp:read"}}});

    mcp::auth::OAuthStepUpExecutionRequest request;
    request.resource = "https://mcp.example.com/mcp";
    request.protectedResourceRequest.url = "https://mcp.example.com/mcp";
    request.protectedResourceRequest.method = "POST";
    request.initialScopes = mcp::auth::OAuthScopeSet {{"mcp:read"}};
    request.maxStepUpAttempts = 5;
    request.tokenStorage = tokenStorage;
    request.requestExecutor = [&requestsSeen](const mcp::auth::OAuthProtectedResourceRequest &) -> mcp::auth::OAuthHttpResponse
    {
      ++requestsSeen;
      return makeInsufficientScopeResponse("mcp:write");
    };
    request.authorizer = [&authorizationCalls](const mcp::auth::OAuthStepUpAuthorizationRequest &authorizationRequest) -> mcp::auth::OAuthAccessToken
    {
      ++authorizationCalls;
      return {
        "token-step-up",
        authorizationRequest.requestedScopes,
      };
    };

    const mcp::auth::OAuthHttpResponse response = mcp::auth::executeProtectedResourceRequestWithStepUp(request);
    REQUIRE(response.statusCode == 403);
    REQUIRE(authorizationCalls == 1);
    REQUIRE(requestsSeen == 2);
  }

  SECTION("changing scope sets are capped by maxStepUpAttempts")
  {
    std::size_t requestsSeen = 0;
    std::size_t authorizationCalls = 0;

    const auto tokenStorage = std::make_shared<mcp::auth::InMemoryOAuthTokenStorage>();
    tokenStorage->save("https://mcp.example.com/mcp", mcp::auth::OAuthAccessToken {"token-initial", mcp::auth::OAuthScopeSet {{"mcp:read"}}});

    mcp::auth::OAuthStepUpExecutionRequest request;
    request.resource = "https://mcp.example.com/mcp";
    request.protectedResourceRequest.url = "https://mcp.example.com/mcp";
    request.protectedResourceRequest.method = "POST";
    request.initialScopes = mcp::auth::OAuthScopeSet {{"mcp:read"}};
    request.maxStepUpAttempts = 2;
    request.tokenStorage = tokenStorage;
    request.requestExecutor = [&requestsSeen](const mcp::auth::OAuthProtectedResourceRequest &) -> mcp::auth::OAuthHttpResponse
    {
      ++requestsSeen;
      return makeInsufficientScopeResponse("mcp:scope-" + std::to_string(requestsSeen));
    };
    request.authorizer = [&authorizationCalls](const mcp::auth::OAuthStepUpAuthorizationRequest &authorizationRequest) -> mcp::auth::OAuthAccessToken
    {
      ++authorizationCalls;
      return {
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

TEST_CASE("Authorization flow enforces SSRF and redirect hardening", "[conformance][authorization]")
{
  SECTION("discovery rejects redirects that change origin")
  {
    MockOAuthEndpoints endpoints("https://mcp.example.com", "https://auth.example.com");
    endpoints.addResourceResponse("/.well-known/oauth-protected-resource/mcp", makeRedirectResponse("https://evil.example.net/metadata"));

    mcp::auth::AuthorizationDiscoveryRequest request;
    request.mcpEndpointUrl = "https://mcp.example.com/mcp";
    request.wwwAuthenticateHeaderValues = {
      "Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\"",
    };
    request.httpFetcher = [&endpoints](const mcp::auth::DiscoveryHttpRequest &httpRequest) -> mcp::auth::DiscoveryHttpResponse { return endpoints.fetch(httpRequest); };
    request.dnsResolver = [](std::string_view) -> std::vector<std::string> { return {"93.184.216.34"}; };

    try
    {
      static_cast<void>(mcp::auth::discoverAuthorizationMetadata(request));
      FAIL("Expected discovery to reject cross-origin redirect");
    }
    catch (const mcp::auth::AuthorizationDiscoveryError &error)
    {
      REQUIRE(error.code() == mcp::auth::AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation);
    }
  }

  SECTION("discovery rejects SSRF to private addresses")
  {
    bool fetchCalled = false;

    mcp::auth::AuthorizationDiscoveryRequest request;
    request.mcpEndpointUrl = "https://mcp.example.com/mcp";
    request.wwwAuthenticateHeaderValues = {
      "Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\"",
    };
    request.httpFetcher = [&fetchCalled](const mcp::auth::DiscoveryHttpRequest &) -> mcp::auth::DiscoveryHttpResponse
    {
      fetchCalled = true;
      return {};
    };
    request.dnsResolver = [](std::string_view) -> std::vector<std::string> { return {"127.0.0.1"}; };

    try
    {
      static_cast<void>(mcp::auth::discoverAuthorizationMetadata(request));
      FAIL("Expected discovery to reject private DNS resolutions");
    }
    catch (const mcp::auth::AuthorizationDiscoveryError &error)
    {
      REQUIRE(error.code() == mcp::auth::AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation);
    }

    REQUIRE_FALSE(fetchCalled);
  }

  SECTION("token request execution rejects cross-origin redirects")
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
        response.headers.push_back({"Location", "https://evil.example.net/token"});
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
      FAIL("Expected token request policy to reject cross-origin redirect");
    }
    catch (const mcp::auth::OAuthClientError &error)
    {
      REQUIRE(error.code() == mcp::auth::OAuthClientErrorCode::kSecurityPolicyViolation);
    }

    REQUIRE(redirectedEndpointCalls == 0);
  }
}

TEST_CASE("OAuth token handling avoids placing tokens in URLs", "[conformance][authorization]")
{
  const mcp::auth::AuthorizationServerMetadata metadata = makeAuthorizationServerMetadata();

  SECTION("authorization URL builder rejects token-like query parameters")
  {
    mcp::auth::OAuthAuthorizationUrlRequest request;
    request.authorizationServerMetadata = metadata;
    request.clientId = "sdk-client";
    request.redirectUri = "http://localhost:8721/callback";
    request.state = "state-123";
    request.codeChallenge = "challenge-value";
    request.resource = "https://mcp.example.com/mcp";
    request.additionalParameters.push_back({"access_token", "leak"});

    try
    {
      static_cast<void>(mcp::auth::buildAuthorizationUrl(request));
      FAIL("Expected authorization URL builder to reject access_token query parameters");
    }
    catch (const mcp::auth::OAuthClientError &error)
    {
      REQUIRE(error.code() == mcp::auth::OAuthClientErrorCode::kSecurityPolicyViolation);
    }
  }

  SECTION("token request builder rejects token-like form parameters")
  {
    mcp::auth::OAuthTokenExchangeRequest request;
    request.authorizationServerMetadata = metadata;
    request.clientId = "sdk-client";
    request.redirectUri = "http://localhost:8721/callback";
    request.authorizationCode = "auth-code";
    request.codeVerifier = std::string(43, 'a');
    request.resource = "https://mcp.example.com/mcp";
    request.additionalParameters.push_back({"refresh_token", "leak"});

    try
    {
      static_cast<void>(mcp::auth::buildTokenExchangeHttpRequest(request));
      FAIL("Expected token request builder to reject refresh_token form parameters");
    }
    catch (const mcp::auth::OAuthClientError &error)
    {
      REQUIRE(error.code() == mcp::auth::OAuthClientErrorCode::kSecurityPolicyViolation);
    }
  }

  SECTION("protected resource requests send bearer token in header instead of URL")
  {
    std::optional<mcp::auth::OAuthProtectedResourceRequest> observedRequest;

    const auto tokenStorage = std::make_shared<mcp::auth::InMemoryOAuthTokenStorage>();
    tokenStorage->save("https://mcp.example.com/mcp", mcp::auth::OAuthAccessToken {"token-secret", mcp::auth::OAuthScopeSet {{"mcp:read"}}});

    mcp::auth::OAuthStepUpExecutionRequest request;
    request.resource = "https://mcp.example.com/mcp";
    request.protectedResourceRequest.method = "POST";
    request.protectedResourceRequest.url = "https://mcp.example.com/mcp";
    request.tokenStorage = tokenStorage;
    request.requestExecutor = [&observedRequest](const mcp::auth::OAuthProtectedResourceRequest &resourceRequest) -> mcp::auth::OAuthHttpResponse
    {
      observedRequest = resourceRequest;
      mcp::auth::OAuthHttpResponse response;
      response.statusCode = 200;
      return response;
    };
    request.authorizer = [](const mcp::auth::OAuthStepUpAuthorizationRequest &) -> mcp::auth::OAuthAccessToken
    {
      FAIL("Authorizer should not be called when request succeeds without step-up");
      return {};
    };

    const mcp::auth::OAuthHttpResponse response = mcp::auth::executeProtectedResourceRequestWithStepUp(request);
    REQUIRE(response.statusCode == 200);
    REQUIRE(observedRequest.has_value());
    REQUIRE(observedRequest->url.find("token-secret") == std::string::npos);
    REQUIRE(oauthHeaderValue(observedRequest->headers, "Authorization") == std::optional<std::string> {"Bearer token-secret"});
  }
}
