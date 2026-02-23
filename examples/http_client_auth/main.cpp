#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/auth/all.hpp>
#include <mcp/auth/oauth_client.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/transport/all.hpp>

namespace
{

namespace mcp_http = mcp::transport::http;

constexpr std::uint16_t kHttpStatusOk = 200;
constexpr std::uint16_t kHttpStatusFound = 302;
constexpr std::uint16_t kHttpStatusBadRequest = 400;
constexpr std::uint16_t kHttpStatusNotFound = 404;
constexpr int kLoopbackTimeoutSeconds = 10;
constexpr int kHexBase = 16;
constexpr int kHexOffset = 10;
constexpr std::string_view kMockAuthBaseUrl = "https://127.0.0.1:9443";

auto hexValue(char c) -> int  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  if (c >= '0' && c <= '9')
  {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f')
  {
    return kHexBase - kHexOffset + (c - 'a');
  }
  if (c >= 'A' && c <= 'F')
  {
    return kHexBase - kHexOffset + (c - 'A');
  }

  return -1;
}

auto decodeUrlComponent(std::string_view encoded) -> std::string  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  std::string decoded;
  decoded.reserve(encoded.size());

  for (std::size_t i = 0; i < encoded.size(); ++i)
  {
    if (encoded[i] == '+')
    {
      decoded.push_back(' ');
      continue;
    }

    if (encoded[i] == '%' && i + 2 < encoded.size())
    {
      const int high = hexValue(encoded[i + 1]);
      const int low = hexValue(encoded[i + 2]);
      if (high >= 0 && low >= 0)
      {
        const auto byte =
          static_cast<unsigned int>((static_cast<unsigned int>(high) << 4) | static_cast<unsigned int>(low));  // NOLINT(hicpp-signed-bitwise,readability-redundant-casting)
        decoded.push_back(static_cast<char>(byte));
        i += 2;
        continue;
      }
    }

    decoded.push_back(encoded[i]);
  }

  return decoded;
}

auto toJsonString(const mcp::jsonrpc::JsonValue &value) -> std::string  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  std::string encoded;
  jsoncons::encode_json(value, encoded);
  return encoded;
}

auto queryParameter(std::string_view pathAndQuery, std::string_view key) -> std::optional<std::string>  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  const std::size_t queryStart = pathAndQuery.find('?');
  if (queryStart == std::string_view::npos || queryStart + 1 >= pathAndQuery.size())
  {
    return std::nullopt;
  }

  const std::string_view query = pathAndQuery.substr(queryStart + 1);
  std::size_t segmentStart = 0;
  while (segmentStart < query.size())
  {
    std::size_t segmentEnd = query.find('&', segmentStart);
    if (segmentEnd == std::string_view::npos)
    {
      segmentEnd = query.size();
    }

    const std::string_view segment = query.substr(segmentStart, segmentEnd - segmentStart);
    const std::size_t equalsPos = segment.find('=');
    if (equalsPos != std::string_view::npos)
    {
      if (segment.substr(0, equalsPos) == key)
      {
        return decodeUrlComponent(segment.substr(equalsPos + 1));
      }
    }

    segmentStart = segmentEnd + 1;
  }

  return std::nullopt;
}

auto extractPathAndQuery(std::string_view absoluteUrl) -> std::string  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  const std::size_t schemePos = absoluteUrl.find("://");
  const std::size_t authorityStart = schemePos == std::string_view::npos ? 0 : schemePos + 3;
  const std::size_t pathStart = absoluteUrl.find('/', authorityStart);
  if (pathStart == std::string_view::npos)
  {
    return "/";
  }

  return std::string(absoluteUrl.substr(pathStart));
}

// NOLINTNEXTLINE(llvm-prefer-static-over-anonymous-namespace, hicpp-special-member-functions)
auto executeMockAuthorizationServerRequest(std::string_view method, std::string_view absoluteUrl, std::string_view body = std::string_view {}) -> mcp_http::ServerResponse
{
  static_cast<void>(body);
  const std::string path = extractPathAndQuery(absoluteUrl);
  const std::string serverBaseUrl(kMockAuthBaseUrl);

  if (method == "GET" && path == "/.well-known/oauth-protected-resource/mcp")
  {
    mcp_http::ServerResponse response;
    response.statusCode = kHttpStatusOk;
    mcp::jsonrpc::JsonValue responseBody = mcp::jsonrpc::JsonValue::object();
    responseBody["resource"] = serverBaseUrl + "/mcp";
    responseBody["authorization_servers"] = mcp::jsonrpc::JsonValue::array();
    responseBody["authorization_servers"].push_back(serverBaseUrl);
    responseBody["scopes_supported"] = mcp::jsonrpc::JsonValue::array();
    responseBody["scopes_supported"].push_back("mcp:read");
    responseBody["scopes_supported"].push_back("mcp:write");
    response.body = toJsonString(responseBody);
    mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "application/json");
    return response;
  }

  if (method == "GET" && path == "/.well-known/oauth-authorization-server")
  {
    mcp_http::ServerResponse response;
    response.statusCode = kHttpStatusOk;
    mcp::jsonrpc::JsonValue responseBody = mcp::jsonrpc::JsonValue::object();
    responseBody["issuer"] = serverBaseUrl;
    responseBody["authorization_endpoint"] = serverBaseUrl + "/oauth/authorize";
    responseBody["token_endpoint"] = serverBaseUrl + "/oauth/token";
    responseBody["code_challenge_methods_supported"] = mcp::jsonrpc::JsonValue::array();
    responseBody["code_challenge_methods_supported"].push_back("S256");
    response.body = toJsonString(responseBody);
    mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "application/json");
    return response;
  }

  if (method == "GET" && path.rfind("/oauth/authorize", 0) == 0)
  {
    const std::optional<std::string> redirectUri = queryParameter(path, "redirect_uri");
    const std::optional<std::string> state = queryParameter(path, "state");
    if (!redirectUri.has_value() || !state.has_value())
    {
      mcp_http::ServerResponse badRequest;
      badRequest.statusCode = kHttpStatusBadRequest;
      badRequest.body = "missing redirect_uri or state";
      return badRequest;
    }

    mcp_http::ServerResponse response;
    response.statusCode = kHttpStatusFound;
    mcp_http::setHeader(response.headers, "Location", *redirectUri + "?code=example-auth-code&state=" + *state);
    return response;
  }

  if (method == "POST" && path == "/oauth/token")
  {
    mcp_http::ServerResponse response;
    response.statusCode = kHttpStatusOk;
    mcp::jsonrpc::JsonValue responseBody = mcp::jsonrpc::JsonValue::object();
    responseBody["access_token"] = "example-access-token";
    responseBody["token_type"] = "Bearer";
    responseBody["scope"] = "mcp:read mcp:write";
    response.body = toJsonString(responseBody);
    mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "application/json");
    return response;
  }

  mcp_http::ServerResponse notFound;
  notFound.statusCode = kHttpStatusNotFound;
  return notFound;
}

auto executeTokenHttpRequest(const mcp::auth::OAuthTokenHttpRequest &request) -> mcp::auth::OAuthHttpResponse  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  const mcp_http::ServerResponse runtimeResponse = executeMockAuthorizationServerRequest(request.method, request.url, request.body);

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

auto executeDiscoveryRequest(const mcp::auth::DiscoveryHttpRequest &request) -> mcp::auth::DiscoveryHttpResponse  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  const mcp_http::ServerResponse runtimeResponse = executeMockAuthorizationServerRequest(request.method, request.url);

  mcp::auth::DiscoveryHttpResponse response;
  response.statusCode = runtimeResponse.statusCode;
  response.body = runtimeResponse.body;
  response.headers.reserve(runtimeResponse.headers.size());
  for (const mcp_http::Header &header : runtimeResponse.headers)
  {
    response.headers.push_back(mcp::auth::DiscoveryHeader {header.name, header.value});
  }

  return response;
}

}  // namespace

#include <jsoncons/encode_json.hpp>

auto main() -> int
{
  try
  {
    const std::string authBaseUrl(kMockAuthBaseUrl);
    const std::string mcpEndpointUrl = authBaseUrl + "/mcp";

    mcp::auth::AuthorizationDiscoveryRequest discoveryRequest;
    discoveryRequest.mcpEndpointUrl = mcpEndpointUrl;
    discoveryRequest.wwwAuthenticateHeaderValues = {
      "Bearer resource_metadata=\"" + authBaseUrl + "/.well-known/oauth-protected-resource/mcp\"",
    };
    discoveryRequest.httpFetcher = executeDiscoveryRequest;
    discoveryRequest.securityPolicy.requireHttps = false;
    discoveryRequest.securityPolicy.allowPrivateAndLocalAddresses = true;

    std::cout << "WARNING: enabling local/private discovery addresses for demo only." << '\n';
    std::cout << "Do not enable this policy in production OAuth clients." << '\n';

    const mcp::auth::AuthorizationDiscoveryResult discovery = mcp::auth::discoverAuthorizationMetadata(discoveryRequest);

    std::cout << "discovered metadata:" << '\n';
    std::cout << "  resource metadata url: " << discovery.protectedResourceMetadataUrl << '\n';
    std::cout << "  authorization server metadata url: " << discovery.authorizationServerMetadataUrl << '\n';

    mcp::auth::LoopbackReceiverOptions receiverOptions;
    receiverOptions.callbackPath = "/oauth/callback";
    receiverOptions.authorizationTimeout = std::chrono::seconds(kLoopbackTimeoutSeconds);

    mcp::auth::LoopbackRedirectReceiver receiver(receiverOptions);
    receiver.start();

    const mcp::auth::PkceCodePair pkce = mcp::auth::generatePkceCodePair();
    const std::string state = "example-state-123";

    mcp::auth::OAuthAuthorizationUrlRequest authorizationUrlRequest;
    authorizationUrlRequest.authorizationServerMetadata = discovery.authorizationServerMetadata;
    authorizationUrlRequest.clientId = "example-client";
    authorizationUrlRequest.redirectUri = receiver.redirectUri();
    authorizationUrlRequest.state = state;
    authorizationUrlRequest.codeChallenge = pkce.codeChallenge;
    authorizationUrlRequest.scopes = discovery.selectedScopes;
    authorizationUrlRequest.resource = discovery.protectedResourceMetadata.resource;

    const std::string authorizationUrl = mcp::auth::buildAuthorizationUrl(authorizationUrlRequest);
    std::cout << "authorization URL: " << authorizationUrl << '\n';

    const mcp_http::ServerResponse authorizationResponse = executeMockAuthorizationServerRequest("GET", authorizationUrl);
    if (authorizationResponse.statusCode != kHttpStatusFound)
    {
      throw std::runtime_error("authorization endpoint did not return expected redirect");
    }

    const std::optional<std::string> callbackLocation = mcp_http::getHeader(authorizationResponse.headers, "Location");
    if (!callbackLocation.has_value())
    {
      throw std::runtime_error("authorization endpoint redirect missing Location header");
    }

    std::string callbackDispatchUrl = *callbackLocation;
    constexpr std::string_view kLoopbackLocalhostPrefix = "http://localhost:";
    if (callbackDispatchUrl.rfind(kLoopbackLocalhostPrefix, 0) == 0)
    {
      callbackDispatchUrl.replace(0, kLoopbackLocalhostPrefix.size(), "http://127.0.0.1:");
    }

    mcp::transport::http::HttpClientOptions callbackOptions;
    callbackOptions.endpointUrl = callbackDispatchUrl;
    const mcp::transport::http::HttpClientRuntime callbackRuntime(callbackOptions);
    mcp_http::ServerRequest callbackRequest;
    callbackRequest.method = mcp_http::ServerRequestMethod::kGet;
    callbackRequest.path = extractPathAndQuery(callbackDispatchUrl);
    const mcp_http::ServerResponse callbackResponse = callbackRuntime.execute(callbackRequest);
    std::cout << "loopback callback URL: " << callbackDispatchUrl << '\n';
    std::cout << "loopback callback status: " << callbackResponse.statusCode << '\n';

    const mcp::auth::LoopbackAuthorizationCode callbackCode = receiver.waitForAuthorizationCode(state);

    mcp::auth::OAuthTokenExchangeRequest tokenExchangeRequest;
    tokenExchangeRequest.authorizationServerMetadata = discovery.authorizationServerMetadata;
    tokenExchangeRequest.clientId = "example-client";
    tokenExchangeRequest.redirectUri = receiver.redirectUri();
    tokenExchangeRequest.authorizationCode = callbackCode.code;
    tokenExchangeRequest.codeVerifier = pkce.codeVerifier;
    tokenExchangeRequest.resource = discovery.protectedResourceMetadata.resource;

    const mcp::auth::OAuthTokenHttpRequest tokenHttpRequest = mcp::auth::buildTokenExchangeHttpRequest(tokenExchangeRequest);

    mcp::auth::OAuthTokenRequestExecutionRequest tokenExecutionRequest;
    tokenExecutionRequest.tokenRequest = tokenHttpRequest;
    tokenExecutionRequest.requestExecutor = executeTokenHttpRequest;
    tokenExecutionRequest.securityPolicy.requireHttps = false;
    tokenExecutionRequest.securityPolicy.requireSameOriginRedirects = false;

    const mcp::auth::OAuthHttpResponse tokenResponse = mcp::auth::executeTokenRequestWithPolicy(tokenExecutionRequest);

    std::cout << "authorization code received: " << callbackCode.code << '\n';
    std::cout << "token response status: " << tokenResponse.statusCode << '\n';
    std::cout << "token response body: " << tokenResponse.body << '\n';

    receiver.stop();
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "http_client_auth failed: " << error.what() << '\n';
    return 1;
  }
}
