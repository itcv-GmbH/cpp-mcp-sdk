#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/auth/oauth_server.hpp>
#include <mcp/http/sse.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/server/server.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/sdk/version.hpp>

namespace
{

namespace mcp_http = mcp::transport::http;

class StaticTokenVerifier final : public mcp::auth::OAuthTokenVerifier
{
public:
  auto verifyToken(const mcp::auth::OAuthTokenVerificationRequest &request) const -> mcp::auth::OAuthTokenVerificationResult override
  {
    if (request.bearerToken != "valid-token")
    {
      return {};
    }

    mcp::auth::OAuthTokenVerificationResult result;
    result.status = mcp::auth::OAuthTokenVerificationStatus::kValid;
    result.audienceBound = true;
    result.authorizationContext.taskIsolationKey = "legacy-user";
    result.authorizationContext.grantedScopes.values = {"mcp:read"};
    return result;
  }
};

auto makeRequest(mcp_http::ServerRequestMethod method,
                 std::string path,
                 std::optional<std::string_view> body = std::nullopt,
                 std::optional<std::string_view> authorization = std::nullopt,
                 std::optional<std::string_view> origin = std::nullopt,
                 std::optional<std::string_view> lastEventId = std::nullopt) -> mcp_http::ServerRequest
{
  mcp_http::ServerRequest request;
  request.method = method;
  request.path = std::move(path);

  if (body.has_value())
  {
    request.body = std::string(*body);
    mcp_http::setHeader(request.headers, mcp_http::kHeaderContentType, "application/json");
  }

  if (authorization.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderAuthorization, std::string(*authorization));
  }

  if (origin.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderOrigin, std::string(*origin));
  }

  if (lastEventId.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderLastEventId, std::string(*lastEventId));
  }

  return request;
}

auto makeInitializeBody(std::int64_t requestId) -> std::string
{
  mcp::jsonrpc::Request request;
  request.id = requestId;
  request.method = "initialize";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["protocolVersion"] = std::string(mcp::kLatestProtocolVersion);
  (*request.params)["capabilities"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["clientInfo"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["clientInfo"]["name"] = "legacy-client";
  (*request.params)["clientInfo"]["version"] = "1.0.0";
  return mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {request});
}

auto makeNotificationBody(std::string method) -> std::string
{
  mcp::jsonrpc::Notification notification;
  notification.method = std::move(method);
  return mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {notification});
}

auto makeToolsListBody(std::int64_t requestId) -> std::string
{
  mcp::jsonrpc::Request request;
  request.id = requestId;
  request.method = "tools/list";
  request.params = mcp::jsonrpc::JsonValue::object();
  return mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {request});
}

class DeterministicLegacyClientFixture
{
public:
  DeterministicLegacyClientFixture(mcp_http::StreamableHttpServer &server, std::string ssePath)
    : server_(server)
    , ssePath_(std::move(ssePath))
  {
  }

  auto openSseAndReadEndpoint() -> void
  {
    const mcp_http::ServerResponse response = server_.handleRequest(makeRequest(mcp_http::ServerRequestMethod::kGet, ssePath_));
    REQUIRE(response.statusCode == 200);

    const std::vector<mcp::http::sse::Event> events = mcp::http::sse::parseEvents(response.body);
    REQUIRE_FALSE(events.empty());
    REQUIRE(events.front().event == std::optional<std::string> {"endpoint"});
    REQUIRE_FALSE(events.front().data.empty());

    postPath_ = events.front().data;
    updateLastEventId(events);
  }

  auto post(std::string_view body) const -> mcp_http::ServerResponse { return server_.handleRequest(makeRequest(mcp_http::ServerRequestMethod::kPost, postPath_, body)); }

  auto readMessageEvents() -> std::vector<mcp::jsonrpc::Message>
  {
    const mcp_http::ServerRequest request = makeRequest(mcp_http::ServerRequestMethod::kGet, ssePath_, std::nullopt, std::nullopt, std::nullopt, lastEventId_);
    const mcp_http::ServerResponse response = server_.handleRequest(request);
    REQUIRE(response.statusCode == 200);

    std::vector<mcp::jsonrpc::Message> messages;
    const std::vector<mcp::http::sse::Event> events = mcp::http::sse::parseEvents(response.body);
    updateLastEventId(events);

    for (const auto &event : events)
    {
      const std::string_view eventName = event.event.has_value() ? std::string_view(*event.event) : std::string_view("message");
      if (eventName == "endpoint")
      {
        continue;
      }

      REQUIRE(eventName == "message");
      if (event.data.empty())
      {
        continue;
      }

      messages.push_back(mcp::jsonrpc::parseMessage(event.data));
    }

    return messages;
  }

private:
  auto updateLastEventId(const std::vector<mcp::http::sse::Event> &events) -> void
  {
    for (const auto &event : events)
    {
      if (event.id.has_value())
      {
        lastEventId_ = event.id;
      }
    }
  }

  mcp_http::StreamableHttpServer &server_;
  std::string ssePath_;
  std::string postPath_;
  std::optional<std::string> lastEventId_;
};

}  // namespace

TEST_CASE("Legacy server compatibility supports initialize and tools/list over HTTP+SSE", "[conformance][legacy_server]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, std::nullopt, mcp::ToolsCapability {}, std::nullopt, std::nullopt);
  auto coreServer = mcp::Server::create(std::move(configuration));

  mcp::ToolDefinition tool;
  tool.name = "legacy-tool";
  tool.inputSchema = mcp::jsonrpc::JsonValue::object();
  tool.inputSchema["type"] = "object";
  coreServer->registerTool(std::move(tool),
                           [](const mcp::ToolCallContext &) -> mcp::CallToolResult
                           {
                             mcp::CallToolResult result;
                             result.content = mcp::jsonrpc::JsonValue::array();
                             return result;
                           });

  mcp_http::StreamableHttpServerOptions options;
  options.enableLegacyHttpSseCompatibility = true;
  options.legacySseEndpointPath = "/events";
  options.legacyPostEndpointPath = "/rpc";

  mcp_http::StreamableHttpServer server(options);
  server.setRequestHandler(
    [&coreServer](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Request &request) -> mcp_http::StreamableRequestResult
    {
      mcp_http::StreamableRequestResult result;
      result.response = coreServer->handleRequest(context, request).get();
      return result;
    });
  server.setNotificationHandler(
    [&coreServer](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Notification &notification) -> bool
    {
      coreServer->handleNotification(context, notification);
      return true;
    });
  server.setResponseHandler([&coreServer](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Response &response) -> bool
                            { return coreServer->handleResponse(context, response); });

  DeterministicLegacyClientFixture client(server, "/events");
  client.openSseAndReadEndpoint();

  const mcp_http::ServerResponse initializeAccepted = client.post(makeInitializeBody(1));
  REQUIRE(initializeAccepted.statusCode == 202);

  const std::vector<mcp::jsonrpc::Message> initializeMessages = client.readMessageEvents();
  REQUIRE(initializeMessages.size() == 1);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initializeMessages.front()));
  REQUIRE(std::get<mcp::jsonrpc::SuccessResponse>(initializeMessages.front()).id == mcp::jsonrpc::RequestId {std::int64_t {1}});

  const mcp_http::ServerResponse initializedAccepted = client.post(makeNotificationBody("notifications/initialized"));
  REQUIRE(initializedAccepted.statusCode == 202);

  const mcp_http::ServerResponse toolsListAccepted = client.post(makeToolsListBody(2));
  REQUIRE(toolsListAccepted.statusCode == 202);

  const std::vector<mcp::jsonrpc::Message> toolsListMessages = client.readMessageEvents();
  REQUIRE(toolsListMessages.size() == 1);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(toolsListMessages.front()));

  const auto &toolsResult = std::get<mcp::jsonrpc::SuccessResponse>(toolsListMessages.front()).result;
  REQUIRE(toolsResult["tools"].is_array());
  REQUIRE(toolsResult["tools"].size() == 1);
  REQUIRE(toolsResult["tools"][0]["name"].as<std::string>() == "legacy-tool");
}

TEST_CASE("Legacy server compatibility remains disabled unless enabled at runtime", "[conformance][legacy_server]")
{
  mcp_http::StreamableHttpServerOptions options;
  options.enableLegacyHttpSseCompatibility = false;

  mcp_http::StreamableHttpServer server(options);

  const mcp_http::ServerResponse sseNotFound = server.handleRequest(makeRequest(mcp_http::ServerRequestMethod::kGet, "/events"));
  REQUIRE(sseNotFound.statusCode == 404);

  const mcp_http::ServerResponse postNotFound = server.handleRequest(makeRequest(mcp_http::ServerRequestMethod::kPost, "/rpc", makeInitializeBody(1)));
  REQUIRE(postNotFound.statusCode == 404);
}

TEST_CASE("Legacy server compatibility applies origin and authorization checks", "[conformance][legacy_server]")
{
  auto verifier = std::make_shared<StaticTokenVerifier>();

  mcp_http::StreamableHttpServerOptions options;
  options.enableLegacyHttpSseCompatibility = true;
  options.http.authorization = mcp::auth::OAuthServerAuthorizationOptions {};
  options.http.authorization->tokenVerifier = std::move(verifier);
  options.http.authorization->protectedResourceMetadata.resource = "https://mcp.example.com/mcp";
  options.http.authorization->protectedResourceMetadata.authorizationServers = {"https://auth.example.com"};
  options.http.authorization->defaultRequiredScopes.values = {"mcp:read"};

  mcp_http::StreamableHttpServer server(options);

  const mcp_http::ServerResponse disallowedOrigin =
    server.handleRequest(makeRequest(mcp_http::ServerRequestMethod::kGet, "/events", std::nullopt, std::nullopt, "https://evil.example"));
  REQUIRE(disallowedOrigin.statusCode == 403);

  const mcp_http::ServerResponse missingAuthGet = server.handleRequest(makeRequest(mcp_http::ServerRequestMethod::kGet, "/events"));
  REQUIRE(missingAuthGet.statusCode == 401);

  const mcp_http::ServerResponse missingAuthPost = server.handleRequest(makeRequest(mcp_http::ServerRequestMethod::kPost, "/rpc", makeInitializeBody(11)));
  REQUIRE(missingAuthPost.statusCode == 401);

  const mcp_http::ServerResponse authorizedGet = server.handleRequest(makeRequest(mcp_http::ServerRequestMethod::kGet, "/events", std::nullopt, "Bearer valid-token"));
  REQUIRE(authorizedGet.statusCode == 200);

  const std::vector<mcp::http::sse::Event> events = mcp::http::sse::parseEvents(authorizedGet.body);
  REQUIRE_FALSE(events.empty());
  REQUIRE(events.front().event == std::optional<std::string> {"endpoint"});
}
