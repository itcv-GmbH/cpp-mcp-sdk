#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/auth/client_registration.hpp>
#include <mcp/jsonrpc/messages.hpp>

namespace
{

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

}  // namespace

TEST_CASE("CIMD strategy is selected only with AS support and HTTPS client_id", "[auth][registration]")
{
  mcp::auth::ResolveClientRegistrationRequest request;
  request.authorizationServerMetadata.issuer = "https://auth.example.com";
  request.authorizationServerMetadata.registrationEndpoint = "https://auth.example.com/register";
  request.strategyConfiguration.clientIdMetadataDocument = mcp::auth::ClientIdMetadataDocumentConfiguration {
    "https://client.example.com/oauth/client-metadata.json",
    "Example MCP Client",
    {"http://127.0.0.1:8721/callback"},
    mcp::auth::ClientAuthenticationMethod::kNone,
    std::nullopt,
    std::nullopt,
  };
  request.strategyConfiguration.dynamic.enabled = true;
  request.strategyConfiguration.dynamic.clientName = "Dynamic Fallback Client";
  request.strategyConfiguration.dynamic.redirectUris = {"http://127.0.0.1:8721/callback"};

  std::uint32_t dynamicCalls = 0;
  request.httpExecutor = [&dynamicCalls](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
  {
    ++dynamicCalls;
    return makeDynamicRegistrationSuccessResponse("dyn-client-001", {"http://127.0.0.1:8721/callback"}, "none");
  };

  SECTION("selects CIMD when AS advertises support")
  {
    request.authorizationServerMetadata.clientIdMetadataDocumentSupported = true;
    const mcp::auth::ClientRegistrationResult result = mcp::auth::resolveClientRegistration(request);

    REQUIRE(result.strategy == mcp::auth::ClientRegistrationStrategy::kClientIdMetadataDocument);
    REQUIRE(result.client.clientId == "https://client.example.com/oauth/client-metadata.json");
    REQUIRE(result.client.redirectUris == std::vector<std::string> {"http://127.0.0.1:8721/callback"});
    REQUIRE(dynamicCalls == 0);
  }

  SECTION("falls back to dynamic registration when CIMD support is not advertised")
  {
    request.authorizationServerMetadata.clientIdMetadataDocumentSupported = false;
    const mcp::auth::ClientRegistrationResult result = mcp::auth::resolveClientRegistration(request);

    REQUIRE(result.strategy == mcp::auth::ClientRegistrationStrategy::kDynamic);
    REQUIRE(result.client.clientId == "dyn-client-001");
    REQUIRE(dynamicCalls == 1);
  }

  SECTION("rejects non-HTTPS CIMD client_id URLs")
  {
    request.authorizationServerMetadata.clientIdMetadataDocumentSupported = true;
    request.strategyConfiguration.clientIdMetadataDocument->clientId = "http://client.example.com/oauth/client-metadata.json";

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected invalid CIMD client_id URL to fail");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kInvalidInput);
    }
  }
}

TEST_CASE("Dynamic registration is selected only when supported and enabled", "[auth][registration]")
{
  mcp::auth::ResolveClientRegistrationRequest request;
  request.authorizationServerMetadata.issuer = "https://auth.example.com";
  request.authorizationServerMetadata.registrationEndpoint = "https://auth.example.com/register";
  request.authorizationServerMetadata.clientIdMetadataDocumentSupported = false;
  request.strategyConfiguration.dynamic.enabled = true;
  request.strategyConfiguration.dynamic.clientName = "SDK Dynamic Client";
  request.strategyConfiguration.dynamic.redirectUris = {
    "http://127.0.0.1:8721/callback",
  };

  std::vector<mcp::auth::ClientRegistrationHttpRequest> capturedRequests;
  request.httpExecutor = [&capturedRequests](const mcp::auth::ClientRegistrationHttpRequest &httpRequest) -> mcp::auth::ClientRegistrationHttpResponse
  {
    capturedRequests.push_back(httpRequest);
    return makeDynamicRegistrationSuccessResponse("dyn-client-123", {"http://127.0.0.1:8721/callback"}, "none");
  };

  SECTION("selects dynamic strategy when endpoint is present and enabled")
  {
    const mcp::auth::ClientRegistrationResult result = mcp::auth::resolveClientRegistration(request);

    REQUIRE(result.strategy == mcp::auth::ClientRegistrationStrategy::kDynamic);
    REQUIRE(result.client.clientId == "dyn-client-123");
    REQUIRE(capturedRequests.size() == 1);
    REQUIRE(capturedRequests[0].method == "POST");
    REQUIRE(capturedRequests[0].url == "https://auth.example.com/register");
  }

  SECTION("does not select dynamic strategy when it is disabled")
  {
    request.strategyConfiguration.dynamic.enabled = false;

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected host interaction error when dynamic registration is disabled");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kHostInteractionRequired);
    }

    REQUIRE(capturedRequests.empty());
  }
}

TEST_CASE("Client registration errors when no strategy is feasible", "[auth][registration]")
{
  mcp::auth::ResolveClientRegistrationRequest request;
  request.authorizationServerMetadata.issuer = "https://auth.example.com";
  request.authorizationServerMetadata.clientIdMetadataDocumentSupported = false;
  request.authorizationServerMetadata.registrationEndpoint = std::nullopt;
  request.strategyConfiguration.dynamic.enabled = true;

  try
  {
    static_cast<void>(mcp::auth::resolveClientRegistration(request));
    FAIL("Expected host interaction required error");
  }
  catch (const mcp::auth::ClientRegistrationError &error)
  {
    REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kHostInteractionRequired);
    REQUIRE(std::string(error.what()).find("Provide pre-registered client information") != std::string::npos);
  }
}
