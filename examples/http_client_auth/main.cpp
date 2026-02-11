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

#include <jsoncons/json.hpp>
#include <mcp/auth/loopback_receiver.hpp>
#include <mcp/auth/oauth_client.hpp>
#include <mcp/auth/protected_resource_metadata.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/transport/http.hpp>

namespace
{

namespace mcp_http = mcp::transport::http;

constexpr std::uint16_t kHttpStatusOk = 200;
constexpr std::uint16_t kHttpStatusFound = 302;
constexpr std::uint16_t kHttpStatusBadRequest = 400;
constexpr std::uint16_t kHttpStatusNotFound = 404;
constexpr int kLoopbackTimeoutSeconds = 10;
constexpr int kHexBase = 16;

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

auto hexValue(char c) -> int
{
  if (c >= '0' && c <= '9')
  {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f')
  {
    return kHexBase - 6 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F')
  {
    return kHexBase - 6 + (c - 'A');
  }

  return -1;
}

auto decodeUrlComponent(std::string_view encoded) -> std::string
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
        const auto byte = static_cast<unsigned int>((static_cast<unsigned int>(high) << 4U) | static_cast<unsigned int>(low));
        decoded.push_back(static_cast<char>(byte));
        i += 2;
        continue;
      }
    }

    decoded.push_back(encoded[i]);
  }

  return decoded;
}

auto toJsonString(const mcp::jsonrpc::JsonValue &value) -> std::string
{
  std::string encoded;
  jsoncons::encode_json(value, encoded);
  return encoded;
}

auto queryParameter(std::string_view pathAndQuery, std::string_view key) -> std::optional<std::string>
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

auto executeTokenHttpRequest(const mcp::auth::OAuthTokenHttpRequest &request) -> mcp::auth::OAuthHttpResponse
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

auto executeDiscoveryRequest(const mcp::auth::DiscoveryHttpRequest &request) -> mcp::auth::DiscoveryHttpResponse
{
  mcp::transport::HttpClientOptions options;
  options.endpointUrl = request.url;

  const mcp::transport::HttpClientRuntime runtime(options);

  mcp_http::ServerRequest runtimeRequest;
  runtimeRequest.method = toServerRequestMethod(request.method);
  runtimeRequest.path.clear();
  runtimeRequest.headers.reserve(request.headers.size());
  for (const mcp::auth::DiscoveryHeader &header : request.headers)
  {
    runtimeRequest.headers.push_back(mcp_http::Header {header.name, header.value});
  }

  const mcp_http::ServerResponse runtimeResponse = runtime.execute(runtimeRequest);

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

auto main() -> int
{
  try
  {
    mcp::transport::HttpServerOptions authServerOptions;
    authServerOptions.endpoint.bindAddress = "127.0.0.1";
    authServerOptions.endpoint.bindLocalhostOnly = true;
    authServerOptions.endpoint.port = 0;

    mcp::transport::HttpServerRuntime authServer(authServerOptions);

    authServer.setRequestHandler(
      [&authServer](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse
      {
        const std::string serverBaseUrl = "http://127.0.0.1:" + std::to_string(authServer.localPort());

        if (request.method == mcp_http::ServerRequestMethod::kGet && request.path == "/.well-known/oauth-protected-resource/mcp")
        {
          mcp_http::ServerResponse response;
          response.statusCode = kHttpStatusOk;
          mcp::jsonrpc::JsonValue body = mcp::jsonrpc::JsonValue::object();
          body["resource"] = serverBaseUrl + "/mcp";
          body["authorization_servers"] = mcp::jsonrpc::JsonValue::array();
          body["authorization_servers"].push_back(serverBaseUrl);
          body["scopes_supported"] = mcp::jsonrpc::JsonValue::array();
          body["scopes_supported"].push_back("mcp:read");
          body["scopes_supported"].push_back("mcp:write");
          response.body = toJsonString(body);
          mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "application/json");
          return response;
        }

        if (request.method == mcp_http::ServerRequestMethod::kGet && request.path == "/.well-known/oauth-authorization-server")
        {
          mcp_http::ServerResponse response;
          response.statusCode = kHttpStatusOk;
          mcp::jsonrpc::JsonValue body = mcp::jsonrpc::JsonValue::object();
          body["issuer"] = serverBaseUrl;
          body["authorization_endpoint"] = serverBaseUrl + "/oauth/authorize";
          body["token_endpoint"] = serverBaseUrl + "/oauth/token";
          body["code_challenge_methods_supported"] = mcp::jsonrpc::JsonValue::array();
          body["code_challenge_methods_supported"].push_back("S256");
          response.body = toJsonString(body);
          mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "application/json");
          return response;
        }

        if (request.method == mcp_http::ServerRequestMethod::kGet && request.path.rfind("/oauth/authorize", 0) == 0)
        {
          const std::optional<std::string> redirectUri = queryParameter(request.path, "redirect_uri");
          const std::optional<std::string> state = queryParameter(request.path, "state");
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

        if (request.method == mcp_http::ServerRequestMethod::kPost && request.path == "/oauth/token")
        {
          mcp_http::ServerResponse response;
          response.statusCode = kHttpStatusOk;
          mcp::jsonrpc::JsonValue body = mcp::jsonrpc::JsonValue::object();
          body["access_token"] = "example-access-token";
          body["token_type"] = "Bearer";
          body["scope"] = "mcp:read mcp:write";
          response.body = toJsonString(body);
          mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "application/json");
          return response;
        }

        mcp_http::ServerResponse notFound;
        notFound.statusCode = kHttpStatusNotFound;
        return notFound;
      });

    authServer.start();
    const std::string authBaseUrl = "http://127.0.0.1:" + std::to_string(authServer.localPort());
    const std::string mcpEndpointUrl = authBaseUrl + "/mcp";

    mcp::auth::AuthorizationDiscoveryRequest discoveryRequest;
    discoveryRequest.mcpEndpointUrl = mcpEndpointUrl;
    discoveryRequest.wwwAuthenticateHeaderValues = {
      "Bearer resource_metadata=\"" + authBaseUrl + "/.well-known/oauth-protected-resource/mcp\"",
    };
    discoveryRequest.httpFetcher = executeDiscoveryRequest;
    discoveryRequest.dnsResolver = [](std::string_view) -> std::vector<std::string>
    {
      return {
        "93.184.216.34",
      };
    };
    discoveryRequest.securityPolicy.requireHttps = false;

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

    mcp::transport::HttpClientOptions browserOptions;
    browserOptions.endpointUrl = authorizationUrl;
    const mcp::transport::HttpClientRuntime browserRuntime(browserOptions);

    mcp_http::ServerRequest browserRequest;
    browserRequest.method = mcp_http::ServerRequestMethod::kGet;
    const mcp_http::ServerResponse authorizationResponse = browserRuntime.execute(browserRequest);
    if (authorizationResponse.statusCode != kHttpStatusFound)
    {
      throw std::runtime_error("authorization endpoint did not return expected redirect");
    }

    const std::optional<std::string> callbackLocation = mcp_http::getHeader(authorizationResponse.headers, "Location");
    if (!callbackLocation.has_value())
    {
      throw std::runtime_error("authorization endpoint redirect missing Location header");
    }

    mcp::transport::HttpClientOptions callbackOptions;
    callbackOptions.endpointUrl = *callbackLocation;
    const mcp::transport::HttpClientRuntime callbackRuntime(callbackOptions);
    mcp_http::ServerRequest callbackRequest;
    callbackRequest.method = mcp_http::ServerRequestMethod::kGet;
    static_cast<void>(callbackRuntime.execute(callbackRequest));

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
    authServer.stop();
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "http_client_auth failed: " << error.what() << '\n';
    return 1;
  }
}
