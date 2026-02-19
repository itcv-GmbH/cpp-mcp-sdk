#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/auth/protected_resource_metadata.hpp>

// NOLINTBEGIN(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness)

namespace
{

class CannedDiscoveryTransport
{
public:
  auto addResponse(std::string url, mcp::auth::DiscoveryHttpResponse response) -> void { responses_[std::move(url)].push_back(std::move(response)); }

  auto fetch(const mcp::auth::DiscoveryHttpRequest &request) -> mcp::auth::DiscoveryHttpResponse
  {
    requestedUrls_.push_back(request.url);

    auto responseQueue = responses_.find(request.url);
    if (responseQueue == responses_.end() || responseQueue->second.empty())
    {
      throw std::runtime_error("No canned discovery response for URL: " + request.url);
    }

    mcp::auth::DiscoveryHttpResponse response = responseQueue->second.front();
    responseQueue->second.erase(responseQueue->second.begin());
    return response;
  }

  [[nodiscard]] auto requestedUrls() const -> const std::vector<std::string> & { return requestedUrls_; }

private:
  std::unordered_map<std::string, std::vector<mcp::auth::DiscoveryHttpResponse>> responses_;
  std::vector<std::string> requestedUrls_;
};

auto makeDnsResolver(std::unordered_map<std::string, std::vector<std::string>> mappings, std::vector<std::string> defaultAddresses = {"93.184.216.34"})
  -> mcp::auth::DiscoveryDnsResolver
{
  return [mappings = std::move(mappings), defaultAddresses = std::move(defaultAddresses)](std::string_view host) -> std::vector<std::string>
  {
    const auto mapped = mappings.find(std::string(host));
    if (mapped != mappings.end())
    {
      return mapped->second;
    }

    return defaultAddresses;
  };
}

auto makeJsonResponse(std::uint16_t statusCode, std::string body) -> mcp::auth::DiscoveryHttpResponse
{
  mcp::auth::DiscoveryHttpResponse response;
  response.statusCode = statusCode;
  response.body = std::move(body);
  response.headers.push_back({"Content-Type", "application/json"});
  return response;
}

}  // namespace

TEST_CASE("WWW-Authenticate Bearer parser extracts challenges from mixed multi-header schemes", "[auth][discovery]")
{
  const std::vector<std::string> headers = {
    "Digest realm=\"legacy\", nonce=\"abc123\"",
    "Basic realm=\"legacy\"",
    "Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\", scope=\"  mcp:read mcp:write  \"",
    "Negotiate token68",
    "Bearer error=\"insufficient_scope\", scope=\"mcp:admin\"",
  };

  const auto challenges = mcp::auth::parseBearerWwwAuthenticateChallenges(headers);
  REQUIRE(challenges.size() == 2);

  REQUIRE(challenges[0].resourceMetadata.has_value());
  REQUIRE(*challenges[0].resourceMetadata == "https://mcp.example.com/.well-known/oauth-protected-resource/mcp");
  REQUIRE(challenges[0].scope.has_value());
  REQUIRE(*challenges[0].scope == "  mcp:read mcp:write  ");

  REQUIRE(challenges[1].error.has_value());
  REQUIRE(*challenges[1].error == "insufficient_scope");
  REQUIRE(challenges[1].scope.has_value());
  REQUIRE(*challenges[1].scope == "mcp:admin");
}

TEST_CASE("WWW-Authenticate Bearer parser handles multiple quoted challenges in one header", "[auth][discovery]")
{
  const std::vector<std::string> headers = {
    "Digest realm=\"legacy\", Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\", scope=\"mcp:read,mcp:write\", Bearer "
    "error=\"insufficient_scope\", error_description=\"Need \\\"admin\\\", token\", scope=\"mcp:admin\"",
  };

  const auto challenges = mcp::auth::parseBearerWwwAuthenticateChallenges(headers);
  REQUIRE(challenges.size() == 2);

  REQUIRE(challenges[0].resourceMetadata.has_value());
  REQUIRE(*challenges[0].resourceMetadata == "https://mcp.example.com/.well-known/oauth-protected-resource/mcp");
  REQUIRE(challenges[0].scope.has_value());
  REQUIRE(*challenges[0].scope == "mcp:read,mcp:write");

  REQUIRE(challenges[1].error.has_value());
  REQUIRE(*challenges[1].error == "insufficient_scope");
  REQUIRE(challenges[1].scope.has_value());
  REQUIRE(*challenges[1].scope == "mcp:admin");

  std::optional<std::string> errorDescription;
  for (const auto &parameter : challenges[1].parameters)
  {
    if (parameter.name == "error_description")
    {
      errorDescription = parameter.value;
      break;
    }
  }

  REQUIRE(errorDescription.has_value());
  REQUIRE(*errorDescription == "Need \"admin\", token");
}

TEST_CASE("Scope selection handles commas, spaces, and trimming in challenge scope values", "[auth][discovery]")
{
  mcp::auth::ProtectedResourceMetadata metadata;

  SECTION("comma-delimited scope remains a single scope token")
  {
    std::vector<mcp::auth::BearerWwwAuthenticateChallenge> challenges(1);
    challenges[0].scope = "   mcp:read,mcp:write   ";

    const auto selectedScopes = mcp::auth::selectScopesForAuthorization(challenges, metadata);
    REQUIRE(selectedScopes.has_value());
    REQUIRE(selectedScopes->values == std::vector<std::string> {"mcp:read,mcp:write"});
  }

  SECTION("space-delimited scope is split, trimmed, and de-duplicated")
  {
    std::vector<mcp::auth::BearerWwwAuthenticateChallenge> challenges(1);
    challenges[0].scope = "   mcp:read   mcp:write   mcp:read   ";

    const auto selectedScopes = mcp::auth::selectScopesForAuthorization(challenges, metadata);
    REQUIRE(selectedScopes.has_value());
    REQUIRE(selectedScopes->values
            == std::vector<std::string> {
              "mcp:read",
              "mcp:write",
            });
  }
}

TEST_CASE("WWW-Authenticate Bearer parser safely ignores malformed quoted parameters", "[auth][discovery]")
{
  const std::vector<std::string> headers = {
    "Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\", scope=\"mcp:read",
    "Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\"\\oops, scope=\"mcp:write\"",
    "Bearer error_description=\"missing trailing escape\\",
  };

  std::vector<mcp::auth::BearerWwwAuthenticateChallenge> challenges;
  REQUIRE_NOTHROW(challenges = mcp::auth::parseBearerWwwAuthenticateChallenges(headers));
  REQUIRE(challenges.size() == 3);

  REQUIRE(challenges[0].resourceMetadata.has_value());
  REQUIRE(*challenges[0].resourceMetadata == "https://mcp.example.com/.well-known/oauth-protected-resource/mcp");
  REQUIRE_FALSE(challenges[0].scope.has_value());

  REQUIRE_FALSE(challenges[1].resourceMetadata.has_value());
  REQUIRE(challenges[1].scope.has_value());
  REQUIRE(*challenges[1].scope == "mcp:write");

  REQUIRE(challenges[2].parameters.empty());
  REQUIRE_FALSE(challenges[2].resourceMetadata.has_value());
  REQUIRE_FALSE(challenges[2].scope.has_value());
}

TEST_CASE("Discovery uses WWW-Authenticate resource_metadata and RFC8414/OIDC path insertion order", "[auth][discovery]")
{
  CannedDiscoveryTransport transport;

  transport.addResponse("https://mcp.example.com/.well-known/oauth-protected-resource/public/mcp",
                        makeJsonResponse(200,
                                         R"({
                       "resource": "https://mcp.example.com/public/mcp",
                       "authorization_servers": ["https://auth.example.com/tenant1"],
                       "scopes_supported": ["mcp:read", "mcp:write"]
                     })"));

  transport.addResponse("https://auth.example.com/.well-known/oauth-authorization-server/tenant1", makeJsonResponse(404, ""));
  transport.addResponse("https://auth.example.com/.well-known/openid-configuration/tenant1",
                        makeJsonResponse(200,
                                         R"({
                       "issuer": "https://auth.example.com/tenant1",
                       "authorization_endpoint": "https://auth.example.com/tenant1/oauth2/v1/authorize",
                       "token_endpoint": "https://auth.example.com/tenant1/oauth2/v1/token",
                       "code_challenge_methods_supported": ["S256"]
                     })"));

  mcp::auth::AuthorizationDiscoveryRequest request;
  request.mcpEndpointUrl = "https://mcp.example.com/public/mcp";
  request.wwwAuthenticateHeaderValues = {
    "Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/public/mcp\", scope=\"mcp:read\"",
  };
  request.httpFetcher = [&transport](const mcp::auth::DiscoveryHttpRequest &httpRequest) -> mcp::auth::DiscoveryHttpResponse { return transport.fetch(httpRequest); };
  request.dnsResolver = makeDnsResolver({});

  const mcp::auth::AuthorizationDiscoveryResult result = mcp::auth::discoverAuthorizationMetadata(request);

  REQUIRE(result.protectedResourceMetadataUrl == "https://mcp.example.com/.well-known/oauth-protected-resource/public/mcp");
  REQUIRE(result.selectedAuthorizationServer == "https://auth.example.com/tenant1");
  REQUIRE(result.authorizationServerMetadataUrl == "https://auth.example.com/.well-known/openid-configuration/tenant1");
  REQUIRE(result.authorizationServerMetadata.authorizationEndpoint.has_value());
  REQUIRE(result.authorizationServerMetadata.tokenEndpoint.has_value());
  REQUIRE(result.selectedScopes.has_value());
  REQUIRE(result.selectedScopes->values == std::vector<std::string> {"mcp:read"});

  REQUIRE(transport.requestedUrls()
          == std::vector<std::string> {
            "https://mcp.example.com/.well-known/oauth-protected-resource/public/mcp",
            "https://auth.example.com/.well-known/oauth-authorization-server/tenant1",
            "https://auth.example.com/.well-known/openid-configuration/tenant1",
          });
}

TEST_CASE("Discovery falls back to RFC9728 well-known probing order and uses scopes_supported when challenge scope is absent", "[auth][discovery]")
{
  CannedDiscoveryTransport transport;

  transport.addResponse("https://mcp.example.com/.well-known/oauth-protected-resource/public/mcp", makeJsonResponse(404, ""));
  transport.addResponse("https://mcp.example.com/.well-known/oauth-protected-resource",
                        makeJsonResponse(200,
                                         R"({
                       "resource": "https://mcp.example.com/public/mcp",
                       "authorization_servers": ["https://auth.example.com"],
                       "scopes_supported": ["mcp:read", "mcp:write"]
                     })"));

  transport.addResponse("https://auth.example.com/.well-known/oauth-authorization-server",
                        makeJsonResponse(200,
                                         R"({
                       "issuer": "https://auth.example.com",
                       "authorization_endpoint": "https://auth.example.com/authorize",
                       "token_endpoint": "https://auth.example.com/token"
                     })"));

  mcp::auth::AuthorizationDiscoveryRequest request;
  request.mcpEndpointUrl = "https://mcp.example.com/public/mcp";
  request.wwwAuthenticateHeaderValues = {
    "Bearer realm=\"mcp\"",
  };
  request.httpFetcher = [&transport](const mcp::auth::DiscoveryHttpRequest &httpRequest) -> mcp::auth::DiscoveryHttpResponse { return transport.fetch(httpRequest); };
  request.dnsResolver = makeDnsResolver({});

  const mcp::auth::AuthorizationDiscoveryResult result = mcp::auth::discoverAuthorizationMetadata(request);

  REQUIRE(result.protectedResourceMetadataUrl == "https://mcp.example.com/.well-known/oauth-protected-resource");
  REQUIRE(result.selectedAuthorizationServer == "https://auth.example.com");
  REQUIRE(result.authorizationServerMetadataUrl == "https://auth.example.com/.well-known/oauth-authorization-server");
  REQUIRE(result.selectedScopes.has_value());
  REQUIRE(result.selectedScopes->values
          == std::vector<std::string> {
            "mcp:read",
            "mcp:write",
          });

  REQUIRE(transport.requestedUrls()
          == std::vector<std::string> {
            "https://mcp.example.com/.well-known/oauth-protected-resource/public/mcp",
            "https://mcp.example.com/.well-known/oauth-protected-resource",
            "https://auth.example.com/.well-known/oauth-authorization-server",
          });
}

TEST_CASE("Discovery rejects non-HTTPS WWW-Authenticate resource_metadata URLs", "[auth][discovery][security]")
{
  bool fetchCalled = false;

  mcp::auth::AuthorizationDiscoveryRequest request;
  request.mcpEndpointUrl = "https://mcp.example.com/mcp";
  request.wwwAuthenticateHeaderValues = {
    "Bearer resource_metadata=\"http://mcp.example.com/.well-known/oauth-protected-resource/mcp\", scope=\"mcp:read\"",
  };
  request.httpFetcher = [&fetchCalled](const mcp::auth::DiscoveryHttpRequest &) -> mcp::auth::DiscoveryHttpResponse
  {
    fetchCalled = true;
    throw std::runtime_error("HTTP fetch should not be attempted for non-HTTPS metadata URLs");
  };
  request.dnsResolver = makeDnsResolver({
    {"mcp.example.com", {"93.184.216.34"}},
  });

  try
  {
    static_cast<void>(mcp::auth::discoverAuthorizationMetadata(request));
    FAIL("Expected discovery to reject non-HTTPS resource_metadata URL");
  }
  catch (const mcp::auth::AuthorizationDiscoveryError &error)
  {
    REQUIRE(error.code() == mcp::auth::AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation);
  }

  REQUIRE_FALSE(fetchCalled);
}

TEST_CASE("Discovery blocks redirects that change origin", "[auth][discovery][security]")
{
  CannedDiscoveryTransport transport;

  mcp::auth::DiscoveryHttpResponse redirectResponse;
  redirectResponse.statusCode = 302;
  redirectResponse.headers.push_back({"Location", "https://evil.example.net/metadata"});
  transport.addResponse("https://mcp.example.com/.well-known/oauth-protected-resource/mcp", redirectResponse);

  mcp::auth::AuthorizationDiscoveryRequest request;
  request.mcpEndpointUrl = "https://mcp.example.com/mcp";
  request.wwwAuthenticateHeaderValues = {
    "Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\"",
  };
  request.httpFetcher = [&transport](const mcp::auth::DiscoveryHttpRequest &httpRequest) -> mcp::auth::DiscoveryHttpResponse { return transport.fetch(httpRequest); };
  request.dnsResolver = makeDnsResolver({
    {"mcp.example.com", {"93.184.216.34"}},
    {"evil.example.net", {"93.184.216.34"}},
  });

  try
  {
    static_cast<void>(mcp::auth::discoverAuthorizationMetadata(request));
    FAIL("Expected discovery to reject origin-changing redirect");
  }
  catch (const mcp::auth::AuthorizationDiscoveryError &error)
  {
    REQUIRE(error.code() == mcp::auth::AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation);
  }
}

TEST_CASE("Discovery blocks explicit HTTPS downgrade redirects", "[auth][discovery][security]")
{
  CannedDiscoveryTransport transport;

  mcp::auth::DiscoveryHttpResponse redirectResponse;
  redirectResponse.statusCode = 302;
  redirectResponse.headers.push_back({"Location", "http://mcp.example.com/downgraded-metadata"});
  transport.addResponse("https://mcp.example.com/.well-known/oauth-protected-resource/mcp", redirectResponse);

  mcp::auth::AuthorizationDiscoveryRequest request;
  request.mcpEndpointUrl = "https://mcp.example.com/mcp";
  request.wwwAuthenticateHeaderValues = {
    "Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\"",
  };
  request.httpFetcher = [&transport](const mcp::auth::DiscoveryHttpRequest &httpRequest) -> mcp::auth::DiscoveryHttpResponse { return transport.fetch(httpRequest); };
  request.dnsResolver = makeDnsResolver({
    {"mcp.example.com", {"93.184.216.34"}},
  });
  request.securityPolicy.requireSameOriginRedirects = false;

  try
  {
    static_cast<void>(mcp::auth::discoverAuthorizationMetadata(request));
    FAIL("Expected discovery to reject HTTPS downgrade redirect");
  }
  catch (const mcp::auth::AuthorizationDiscoveryError &error)
  {
    REQUIRE(error.code() == mcp::auth::AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation);
  }

  REQUIRE(transport.requestedUrls() == std::vector<std::string> {"https://mcp.example.com/.well-known/oauth-protected-resource/mcp"});
}

TEST_CASE("Discovery blocks SSRF when DNS resolves to private ranges", "[auth][discovery][security]")
{
  mcp::auth::AuthorizationDiscoveryRequest request;
  request.mcpEndpointUrl = "https://mcp.example.com/mcp";
  request.wwwAuthenticateHeaderValues = {
    "Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\"",
  };
  request.httpFetcher = [](const mcp::auth::DiscoveryHttpRequest &) -> mcp::auth::DiscoveryHttpResponse
  { throw std::runtime_error("HTTP fetch should not be attempted when SSRF validation fails"); };
  request.dnsResolver = makeDnsResolver({
    {"mcp.example.com", {"127.0.0.1"}},
  });

  try
  {
    static_cast<void>(mcp::auth::discoverAuthorizationMetadata(request));
    FAIL("Expected discovery to reject private IP DNS resolution");
  }
  catch (const mcp::auth::AuthorizationDiscoveryError &error)
  {
    REQUIRE(error.code() == mcp::auth::AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation);
  }
}

TEST_CASE("Discovery enforces redirect count limits", "[auth][discovery][security]")
{
  CannedDiscoveryTransport transport;

  mcp::auth::DiscoveryHttpResponse firstRedirect;
  firstRedirect.statusCode = 302;
  firstRedirect.headers.push_back({"Location", "/redirect-1"});
  transport.addResponse("https://mcp.example.com/.well-known/oauth-protected-resource/mcp", firstRedirect);

  mcp::auth::DiscoveryHttpResponse secondRedirect;
  secondRedirect.statusCode = 302;
  secondRedirect.headers.push_back({"Location", "/redirect-2"});
  transport.addResponse("https://mcp.example.com/redirect-1", secondRedirect);

  mcp::auth::DiscoveryHttpResponse thirdRedirect;
  thirdRedirect.statusCode = 302;
  thirdRedirect.headers.push_back({"Location", "/redirect-3"});
  transport.addResponse("https://mcp.example.com/redirect-2", thirdRedirect);

  mcp::auth::AuthorizationDiscoveryRequest request;
  request.mcpEndpointUrl = "https://mcp.example.com/mcp";
  request.wwwAuthenticateHeaderValues = {
    "Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\"",
  };
  request.httpFetcher = [&transport](const mcp::auth::DiscoveryHttpRequest &httpRequest) -> mcp::auth::DiscoveryHttpResponse { return transport.fetch(httpRequest); };
  request.dnsResolver = makeDnsResolver({
    {"mcp.example.com", {"93.184.216.34"}},
  });
  request.securityPolicy.maxRedirects = 2;

  try
  {
    static_cast<void>(mcp::auth::discoverAuthorizationMetadata(request));
    FAIL("Expected discovery to enforce max redirect limit");
  }
  catch (const mcp::auth::AuthorizationDiscoveryError &error)
  {
    REQUIRE(error.code() == mcp::auth::AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation);
  }
}

// NOLINTEND(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness)
