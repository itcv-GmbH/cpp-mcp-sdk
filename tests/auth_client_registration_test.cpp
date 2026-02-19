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

TEST_CASE("Dynamic registration fails on non-201 responses with actionable errors", "[auth][registration]")
{
  mcp::auth::ResolveClientRegistrationRequest request;
  request.authorizationServerMetadata.issuer = "https://auth.example.com";
  request.authorizationServerMetadata.registrationEndpoint = "https://auth.example.com/register";
  request.authorizationServerMetadata.clientIdMetadataDocumentSupported = false;
  request.strategyConfiguration.dynamic.enabled = true;
  request.strategyConfiguration.dynamic.clientName = "Test Client";
  request.strategyConfiguration.dynamic.redirectUris = {"http://127.0.0.1:8721/callback"};

  SECTION("400 Bad Request returns kNetworkFailure with status code")
  {
    request.httpExecutor = [](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
    {
      mcp::auth::ClientRegistrationHttpResponse response;
      response.statusCode = 400;
      response.body = R"({"error": "invalid_redirect_uri"})";
      return response;
    };

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected network failure error for 400 status");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kNetworkFailure);
      REQUIRE(std::string(error.what()).find("400") != std::string::npos);
      REQUIRE(std::string(error.what()).find("Dynamic client registration failed") != std::string::npos);
    }
  }

  SECTION("500 Internal Server Error returns kNetworkFailure")
  {
    request.httpExecutor = [](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
    {
      mcp::auth::ClientRegistrationHttpResponse response;
      response.statusCode = 500;
      response.body = R"({"error": "server_error"})";
      return response;
    };

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected network failure error for 500 status");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kNetworkFailure);
      REQUIRE(std::string(error.what()).find("500") != std::string::npos);
    }
  }

  SECTION("401 Unauthorized returns kNetworkFailure")
  {
    request.httpExecutor = [](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
    {
      mcp::auth::ClientRegistrationHttpResponse response;
      response.statusCode = 401;
      response.body = "Unauthorized";
      return response;
    };

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected network failure error for 401 status");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kNetworkFailure);
      REQUIRE(std::string(error.what()).find("401") != std::string::npos);
    }
  }
}

TEST_CASE("Dynamic registration response JSON missing required fields is rejected", "[auth][registration]")
{
  mcp::auth::ResolveClientRegistrationRequest request;
  request.authorizationServerMetadata.issuer = "https://auth.example.com";
  request.authorizationServerMetadata.registrationEndpoint = "https://auth.example.com/register";
  request.authorizationServerMetadata.clientIdMetadataDocumentSupported = false;
  request.strategyConfiguration.dynamic.enabled = true;
  request.strategyConfiguration.dynamic.clientName = "Test Client";
  request.strategyConfiguration.dynamic.redirectUris = {"http://127.0.0.1:8721/callback"};

  SECTION("missing client_id field is rejected")
  {
    request.httpExecutor = [](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
    {
      mcp::auth::ClientRegistrationHttpResponse response;
      response.statusCode = 201;
      response.body = R"({"redirect_uris": ["http://localhost/callback"], "token_endpoint_auth_method": "none"})";
      return response;
    };

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected metadata validation error for missing client_id");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kMetadataValidation);
      REQUIRE(std::string(error.what()).find("client_id") != std::string::npos);
    }
  }

  SECTION("missing redirect_uris in response and no fallback is rejected")
  {
    // Remove fallback redirect URIs from request config
    request.strategyConfiguration.dynamic.redirectUris.clear();

    request.httpExecutor = [](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
    {
      mcp::auth::ClientRegistrationHttpResponse response;
      response.statusCode = 201;
      // Response JSON omits redirect_uris and has no client_secret
      response.body = R"({"client_id": "test-client-123", "token_endpoint_auth_method": "none"})";
      return response;
    };

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected metadata validation error for missing redirect_uris");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kMetadataValidation);
      REQUIRE(std::string(error.what()).find("redirect_uris") != std::string::npos);
    }
  }

  SECTION("invalid JSON is rejected")
  {
    request.httpExecutor = [](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
    {
      mcp::auth::ClientRegistrationHttpResponse response;
      response.statusCode = 201;
      response.body = R"({"invalid json)";
      return response;
    };

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected metadata validation error for invalid JSON");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kMetadataValidation);
    }
  }

  SECTION("non-object JSON is rejected")
  {
    request.httpExecutor = [](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
    {
      mcp::auth::ClientRegistrationHttpResponse response;
      response.statusCode = 201;
      response.body = R"(["client_id", "test"])";
      return response;
    };

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected metadata validation error for non-object JSON");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kMetadataValidation);
      REQUIRE(std::string(error.what()).find("JSON object") != std::string::npos);
    }
  }
}

TEST_CASE("token_endpoint_auth_method parsing allows only supported values", "[auth][registration]")
{
  mcp::auth::ResolveClientRegistrationRequest request;
  request.authorizationServerMetadata.issuer = "https://auth.example.com";
  request.authorizationServerMetadata.registrationEndpoint = "https://auth.example.com/register";
  request.authorizationServerMetadata.clientIdMetadataDocumentSupported = false;
  request.strategyConfiguration.dynamic.enabled = true;
  request.strategyConfiguration.dynamic.clientName = "Test Client";
  request.strategyConfiguration.dynamic.redirectUris = {"http://127.0.0.1:8721/callback"};

  SECTION("'none' auth method is accepted")
  {
    request.httpExecutor = [](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
    { return makeDynamicRegistrationSuccessResponse("client-001", {"http://127.0.0.1:8721/callback"}, "none"); };

    const mcp::auth::ClientRegistrationResult result = mcp::auth::resolveClientRegistration(request);
    REQUIRE(result.client.authenticationMethod == mcp::auth::ClientAuthenticationMethod::kNone);
  }

  SECTION("'client_secret_basic' auth method is accepted")
  {
    request.httpExecutor = [](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
    { return makeDynamicRegistrationSuccessResponse("client-002", {"http://127.0.0.1:8721/callback"}, "client_secret_basic", "secret123"); };

    const mcp::auth::ClientRegistrationResult result = mcp::auth::resolveClientRegistration(request);
    REQUIRE(result.client.authenticationMethod == mcp::auth::ClientAuthenticationMethod::kClientSecretBasic);
    REQUIRE(result.client.clientSecret.has_value());
    REQUIRE(result.client.clientSecret.value() == "secret123");
  }

  SECTION("'client_secret_post' auth method is accepted")
  {
    request.httpExecutor = [](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
    { return makeDynamicRegistrationSuccessResponse("client-003", {"http://127.0.0.1:8721/callback"}, "client_secret_post", "secret456"); };

    const mcp::auth::ClientRegistrationResult result = mcp::auth::resolveClientRegistration(request);
    REQUIRE(result.client.authenticationMethod == mcp::auth::ClientAuthenticationMethod::kClientSecretPost);
    REQUIRE(result.client.clientSecret.has_value());
    REQUIRE(result.client.clientSecret.value() == "secret456");
  }

  SECTION("'private_key_jwt' auth method is accepted")
  {
    request.httpExecutor = [](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
    { return makeDynamicRegistrationSuccessResponse("client-004", {"http://127.0.0.1:8721/callback"}, "private_key_jwt"); };

    const mcp::auth::ClientRegistrationResult result = mcp::auth::resolveClientRegistration(request);
    REQUIRE(result.client.authenticationMethod == mcp::auth::ClientAuthenticationMethod::kPrivateKeyJwt);
  }

  SECTION("unsupported auth method is rejected")
  {
    request.httpExecutor = [](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
    {
      mcp::auth::ClientRegistrationHttpResponse response;
      response.statusCode = 201;
      response.body = R"({"client_id": "client-005", "redirect_uris": ["http://127.0.0.1:8721/callback"], "token_endpoint_auth_method": "tls_client_auth"})";
      return response;
    };

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected metadata validation error for unsupported auth method");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kMetadataValidation);
      REQUIRE(std::string(error.what()).find("tls_client_auth") != std::string::npos);
      REQUIRE(std::string(error.what()).find("Unsupported") != std::string::npos);
    }
  }

  SECTION("empty auth method defaults to none")
  {
    request.httpExecutor = [](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
    {
      mcp::auth::ClientRegistrationHttpResponse response;
      response.statusCode = 201;
      response.body = R"({"client_id": "client-006", "redirect_uris": ["http://127.0.0.1:8721/callback"], "token_endpoint_auth_method": ""})";
      return response;
    };

    const mcp::auth::ClientRegistrationResult result = mcp::auth::resolveClientRegistration(request);
    REQUIRE(result.client.authenticationMethod == mcp::auth::ClientAuthenticationMethod::kNone);
  }

  SECTION("case-insensitive auth method parsing")
  {
    request.httpExecutor = [](const mcp::auth::ClientRegistrationHttpRequest &) -> mcp::auth::ClientRegistrationHttpResponse
    { return makeDynamicRegistrationSuccessResponse("client-007", {"http://127.0.0.1:8721/callback"}, "CLIENT_SECRET_BASIC", "secret789"); };

    const mcp::auth::ClientRegistrationResult result = mcp::auth::resolveClientRegistration(request);
    REQUIRE(result.client.authenticationMethod == mcp::auth::ClientAuthenticationMethod::kClientSecretBasic);
  }
}

TEST_CASE("CIMD inputs validate https scheme and required metadata fields", "[auth][registration]")
{
  mcp::auth::ResolveClientRegistrationRequest request;
  request.authorizationServerMetadata.issuer = "https://auth.example.com";
  request.authorizationServerMetadata.registrationEndpoint = "https://auth.example.com/register";
  request.authorizationServerMetadata.clientIdMetadataDocumentSupported = true;
  request.strategyConfiguration.dynamic.enabled = true;
  request.strategyConfiguration.dynamic.clientName = "Dynamic Fallback Client";
  request.strategyConfiguration.dynamic.redirectUris = {"http://127.0.0.1:8721/callback"};

  SECTION("rejects non-https scheme in CIMD client_id URL")
  {
    request.strategyConfiguration.clientIdMetadataDocument = mcp::auth::ClientIdMetadataDocumentConfiguration {
      "http://client.example.com/oauth/client-metadata.json",
      "Example MCP Client",
      {"http://127.0.0.1:8721/callback"},
      mcp::auth::ClientAuthenticationMethod::kNone,
      std::nullopt,
      std::nullopt,
    };

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected invalid input error for http scheme");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kInvalidInput);
      REQUIRE(std::string(error.what()).find("HTTPS") != std::string::npos);
    }
  }

  SECTION("rejects ftp scheme in CIMD client_id URL")
  {
    request.strategyConfiguration.clientIdMetadataDocument = mcp::auth::ClientIdMetadataDocumentConfiguration {
      "ftp://client.example.com/oauth/client-metadata.json",
      "Example MCP Client",
      {"http://127.0.0.1:8721/callback"},
      mcp::auth::ClientAuthenticationMethod::kNone,
      std::nullopt,
      std::nullopt,
    };

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected invalid input error for ftp scheme");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kInvalidInput);
      REQUIRE(std::string(error.what()).find("HTTPS") != std::string::npos);
    }
  }

  SECTION("rejects missing path component in CIMD client_id URL")
  {
    request.strategyConfiguration.clientIdMetadataDocument = mcp::auth::ClientIdMetadataDocumentConfiguration {
      "https://client.example.com/",
      "Example MCP Client",
      {"http://127.0.0.1:8721/callback"},
      mcp::auth::ClientAuthenticationMethod::kNone,
      std::nullopt,
      std::nullopt,
    };

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected invalid input error for missing path");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kInvalidInput);
      REQUIRE(std::string(error.what()).find("path") != std::string::npos);
    }
  }

  SECTION("rejects empty client_name in CIMD configuration")
  {
    request.strategyConfiguration.clientIdMetadataDocument = mcp::auth::ClientIdMetadataDocumentConfiguration {
      "https://client.example.com/oauth/client-metadata.json",
      "",
      {"http://127.0.0.1:8721/callback"},
      mcp::auth::ClientAuthenticationMethod::kNone,
      std::nullopt,
      std::nullopt,
    };

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected invalid input error for empty client_name");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kInvalidInput);
      REQUIRE(std::string(error.what()).find("client_name") != std::string::npos);
    }
  }

  SECTION("rejects whitespace-only client_name in CIMD configuration")
  {
    request.strategyConfiguration.clientIdMetadataDocument = mcp::auth::ClientIdMetadataDocumentConfiguration {
      "https://client.example.com/oauth/client-metadata.json",
      "   ",
      {"http://127.0.0.1:8721/callback"},
      mcp::auth::ClientAuthenticationMethod::kNone,
      std::nullopt,
      std::nullopt,
    };

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected invalid input error for whitespace client_name");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kInvalidInput);
    }
  }

  SECTION("rejects empty redirect_uris in CIMD configuration")
  {
    request.strategyConfiguration.clientIdMetadataDocument = mcp::auth::ClientIdMetadataDocumentConfiguration {
      "https://client.example.com/oauth/client-metadata.json",
      "Example MCP Client",
      {},
      mcp::auth::ClientAuthenticationMethod::kNone,
      std::nullopt,
      std::nullopt,
    };

    try
    {
      static_cast<void>(mcp::auth::resolveClientRegistration(request));
      FAIL("Expected invalid input error for empty redirect_uris");
    }
    catch (const mcp::auth::ClientRegistrationError &error)
    {
      REQUIRE(error.code() == mcp::auth::ClientRegistrationErrorCode::kInvalidInput);
      REQUIRE(std::string(error.what()).find("redirect_uris") != std::string::npos);
    }
  }

  SECTION("accepts valid HTTPS CIMD client_id URL with path")
  {
    request.strategyConfiguration.clientIdMetadataDocument = mcp::auth::ClientIdMetadataDocumentConfiguration {
      "https://client.example.com/.well-known/oauth/client-metadata.json",
      "Valid Client",
      {"http://127.0.0.1:8721/callback"},
      mcp::auth::ClientAuthenticationMethod::kNone,
      std::nullopt,
      std::nullopt,
    };

    const mcp::auth::ClientRegistrationResult result = mcp::auth::resolveClientRegistration(request);
    REQUIRE(result.strategy == mcp::auth::ClientRegistrationStrategy::kClientIdMetadataDocument);
    REQUIRE(result.client.clientId == "https://client.example.com/.well-known/oauth/client-metadata.json");
  }
}
