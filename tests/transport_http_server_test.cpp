#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/auth/all.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/transport/all.hpp>
#include <mcp/util/all.hpp>

// NOLINTBEGIN(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness)

namespace
{

namespace mcp_http = mcp::transport::http;

auto makeHeaderedRequest(mcp_http::ServerRequestMethod method,
                         std::string path,
                         std::optional<std::string_view> body = std::nullopt,
                         std::optional<std::string_view> sessionId = std::nullopt,
                         std::optional<std::string_view> lastEventId = std::nullopt,
                         std::optional<std::string_view> authorization = std::nullopt,
                         std::optional<std::string_view> contentType = std::optional<std::string_view> {"application/json"}) -> mcp_http::ServerRequest
{
  mcp_http::ServerRequest request;
  request.method = method;
  request.path = std::move(path);

  if (body.has_value())
  {
    request.body = std::string(*body);

    if (contentType.has_value())
    {
      mcp_http::setHeader(request.headers, mcp_http::kHeaderContentType, std::string(*contentType));
    }
  }

  if (sessionId.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderMcpSessionId, std::string(*sessionId));
  }

  if (lastEventId.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderLastEventId, std::string(*lastEventId));
  }

  if (authorization.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderAuthorization, std::string(*authorization));
  }

  return request;
}

auto makeRequestBody(std::int64_t id, std::string method) -> std::string
{
  mcp::jsonrpc::Request request;
  request.id = id;
  request.method = std::move(method);
  return mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {request});
}

auto makeNotificationBody(std::string method) -> std::string
{
  mcp::jsonrpc::Notification notification;
  notification.method = std::move(method);
  return mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {notification});
}

auto messageFromSseEvent(const mcp::http::sse::Event &event) -> mcp::jsonrpc::Message
{
  return mcp::jsonrpc::parseMessage(event.data);
}

auto lastEventId(const std::vector<mcp::http::sse::Event> &events) -> std::optional<std::string>
{
  for (auto it = events.rbegin(); it != events.rend(); ++it)
  {
    if (it->id.has_value())
    {
      return it->id;
    }
  }

  return std::nullopt;
}

class TestTokenVerifier final : public mcp::auth::OAuthTokenVerifier
{
public:
  struct Rule
  {
    mcp::auth::OAuthTokenVerificationStatus status = mcp::auth::OAuthTokenVerificationStatus::kInvalidToken;
    bool audienceBound = false;
    std::string taskIsolationKey;
    std::vector<std::string> grantedScopes;
  };

  auto setRule(std::string token, Rule rule) -> void { rules_[std::move(token)] = std::move(rule); }

  auto verifyToken(const mcp::auth::OAuthTokenVerificationRequest &request) const -> mcp::auth::OAuthTokenVerificationResult override
  {
    const auto rule = rules_.find(request.bearerToken);
    if (rule == rules_.end())
    {
      return {};
    }

    mcp::auth::OAuthTokenVerificationResult result;
    result.status = rule->second.status;
    result.audienceBound = rule->second.audienceBound;
    result.authorizationContext.taskIsolationKey = rule->second.taskIsolationKey;
    result.authorizationContext.grantedScopes.values = rule->second.grantedScopes;
    return result;
  }

private:
  std::unordered_map<std::string, Rule> rules_;
};

auto makeAuthorizationOptions(std::shared_ptr<TestTokenVerifier> verifier, std::string endpointResource = "https://mcp.example.com/mcp")
  -> mcp::auth::OAuthServerAuthorizationOptions
{
  mcp::auth::OAuthServerAuthorizationOptions authorization;
  authorization.tokenVerifier = std::move(verifier);
  authorization.protectedResourceMetadata.resource = std::move(endpointResource);
  authorization.protectedResourceMetadata.authorizationServers = {"https://auth.example.com"};
  authorization.protectedResourceMetadata.scopesSupported.values = {"mcp:read", "mcp:write"};
  authorization.defaultRequiredScopes.values = {"mcp:read"};
  return authorization;
}

auto requireChallengeContains(const mcp_http::ServerResponse &response, std::string_view fragment) -> void
{
  const auto challenge = mcp_http::getHeader(response.headers, mcp_http::kHeaderWwwAuthenticate);
  REQUIRE(challenge.has_value());
  REQUIRE(challenge->find(fragment) != std::string::npos);
}

}  // namespace

TEST_CASE("HTTP server handles POST request with JSON response", "[transport][http][server]")
{
  mcp_http::StreamableHttpServer server;
  server.setRequestHandler(
    [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> mcp_http::StreamableRequestResult
    {
      mcp_http::StreamableRequestResult result;

      mcp::jsonrpc::SuccessResponse response;
      response.id = request.id;
      response.result = mcp::jsonrpc::JsonValue::object();
      response.result["ok"] = true;
      result.response = response;
      return result;
    });

  const std::string body = makeRequestBody(7, "ping");
  const mcp_http::ServerResponse response = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", body));

  REQUIRE(response.statusCode == 200);
  REQUIRE(mcp_http::getHeader(response.headers, mcp_http::kHeaderContentType) == std::optional<std::string> {"application/json"});
  REQUIRE_FALSE(response.sse.has_value());

  const mcp::jsonrpc::Message responseMessage = mcp::jsonrpc::parseMessage(response.body);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(responseMessage));
  REQUIRE(std::get<mcp::jsonrpc::SuccessResponse>(responseMessage).id == mcp::jsonrpc::RequestId {std::int64_t {7}});
}

TEST_CASE("HTTP server handles POST request with SSE pre-response messages", "[transport][http][server]")
{
  mcp_http::StreamableHttpServer server;
  server.setRequestHandler(
    [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> mcp_http::StreamableRequestResult
    {
      mcp_http::StreamableRequestResult result;
      result.useSse = true;

      mcp::jsonrpc::Notification progress;
      progress.method = "notifications/initialized";
      result.preResponseMessages.push_back(mcp::jsonrpc::Message {progress});

      mcp::jsonrpc::SuccessResponse response;
      response.id = request.id;
      response.result = mcp::jsonrpc::JsonValue::object();
      response.result["done"] = true;
      result.response = response;
      return result;
    });

  const std::string body = makeRequestBody(42, "longRunning");
  const mcp_http::ServerResponse response = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", body));

  REQUIRE(response.statusCode == 200);
  REQUIRE(mcp_http::getHeader(response.headers, mcp_http::kHeaderContentType) == std::optional<std::string> {"text/event-stream"});
  REQUIRE(response.sse.has_value());
  REQUIRE(response.sse->terminateStream);
  REQUIRE(response.sse->events.size() == 3);

  const mcp::http::sse::Event &priming = response.sse->events[0];
  REQUIRE(priming.id.has_value());
  REQUIRE(priming.data.empty());
  REQUIRE(mcp::http::sse::parseEventId(*priming.id).has_value());

  const mcp::jsonrpc::Message progressMessage = messageFromSseEvent(response.sse->events[1]);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Notification>(progressMessage));
  REQUIRE(std::get<mcp::jsonrpc::Notification>(progressMessage).method == "notifications/initialized");

  const mcp::jsonrpc::Message finalMessage = messageFromSseEvent(response.sse->events[2]);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(finalMessage));
  REQUIRE(std::get<mcp::jsonrpc::SuccessResponse>(finalMessage).id == mcp::jsonrpc::RequestId {std::int64_t {42}});
}

TEST_CASE("HTTP server accepts POST notifications and responses with 202", "[transport][http][server]")
{
  mcp_http::StreamableHttpServer server;

  SECTION("Notification")
  {
    const std::string body = makeNotificationBody("notifications/initialized");
    const mcp_http::ServerResponse response = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", body));
    REQUIRE(response.statusCode == 202);
    REQUIRE(response.body.empty());
  }

  SECTION("Response")
  {
    mcp::jsonrpc::SuccessResponse success;
    success.id = std::int64_t {3};
    success.result = mcp::jsonrpc::JsonValue::object();
    success.result["ok"] = true;

    const std::string body = mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {success});
    const mcp_http::ServerResponse response = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", body));
    REQUIRE(response.statusCode == 202);
    REQUIRE(response.body.empty());
  }
}

TEST_CASE("HTTP server GET stream carries server-initiated notifications", "[transport][http][server]")
{
  mcp_http::StreamableHttpServer server;

  const mcp_http::ServerResponse openResponse = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp"));
  REQUIRE(openResponse.statusCode == 200);
  REQUIRE(openResponse.sse.has_value());
  REQUIRE(openResponse.sse->events.size() == 1);

  const std::string primingEventId = *openResponse.sse->events.front().id;

  mcp::jsonrpc::Notification outbound;
  outbound.method = "notifications/initialized";
  REQUIRE(server.enqueueServerMessage(mcp::jsonrpc::Message {outbound}));

  const mcp_http::ServerResponse replayResponse =
    server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, std::nullopt, primingEventId));

  REQUIRE(replayResponse.statusCode == 200);
  REQUIRE(replayResponse.sse.has_value());
  REQUIRE(replayResponse.sse->events.size() == 1);

  const mcp::jsonrpc::Message replayedMessage = messageFromSseEvent(replayResponse.sse->events.front());
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Notification>(replayedMessage));
  REQUIRE(std::get<mcp::jsonrpc::Notification>(replayedMessage).method == "notifications/initialized");
}

TEST_CASE("HTTP server Last-Event-ID replay is stream-local", "[transport][http][server]")
{
  mcp_http::StreamableHttpServer server;

  const mcp_http::ServerResponse streamA = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp"));
  const mcp_http::ServerResponse streamB = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp"));

  REQUIRE(streamA.sse.has_value());
  REQUIRE(streamB.sse.has_value());

  const std::string streamAId = *streamA.sse->events.front().id;
  const std::string streamBId = *streamB.sse->events.front().id;

  mcp::jsonrpc::Notification outbound;
  outbound.method = "notifications/initialized";
  REQUIRE(server.enqueueServerMessage(mcp::jsonrpc::Message {outbound}));

  const mcp_http::ServerResponse replayA = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, std::nullopt, streamAId));
  const mcp_http::ServerResponse replayB = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, std::nullopt, streamBId));

  REQUIRE(replayA.sse.has_value());
  REQUIRE(replayA.sse->events.size() == 1);

  const mcp::jsonrpc::Message replayed = messageFromSseEvent(replayA.sse->events.front());
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Notification>(replayed));
  REQUIRE(std::get<mcp::jsonrpc::Notification>(replayed).method == "notifications/initialized");

  REQUIRE(replayB.sse.has_value());
  REQUIRE(replayB.sse->events.empty());
}

TEST_CASE("HTTP server Last-Event-ID resume rejects cross-stream and stale IDs", "[transport][http][server]")
{
  SECTION("Malformed Last-Event-ID is rejected with 400")
  {
    mcp_http::StreamableHttpServer server;

    const mcp_http::ServerResponse malformedResume =
      server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, std::nullopt, "not-a-valid-event-id"));

    REQUIRE(malformedResume.statusCode == 400);
    REQUIRE_FALSE(malformedResume.sse.has_value());
    REQUIRE(malformedResume.body.find("Invalid Last-Event-ID") != std::string::npos);
  }

  SECTION("Cross-stream Last-Event-ID is rejected with 404")
  {
    mcp_http::StreamableHttpServerOptions options;
    options.http.requireSessionId = true;

    mcp_http::StreamableHttpServer server(options);
    server.upsertSession("session-a", mcp_http::SessionLookupState::kActive, std::string(mcp::kLatestProtocolVersion));
    server.upsertSession("session-b", mcp_http::SessionLookupState::kActive, std::string(mcp::kLatestProtocolVersion));

    const mcp_http::ServerResponse streamA = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, "session-a"));
    const mcp_http::ServerResponse streamB = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, "session-b"));

    REQUIRE(streamA.statusCode == 200);
    REQUIRE(streamA.sse.has_value());
    REQUIRE(streamA.sse->events.size() == 1);
    REQUIRE(streamA.sse->events.front().id.has_value());

    REQUIRE(streamB.statusCode == 200);
    REQUIRE(streamB.sse.has_value());

    const std::string streamAEventId = *streamA.sse->events.front().id;
    const mcp_http::ServerResponse crossStreamResume =
      server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, "session-b", streamAEventId));

    REQUIRE(crossStreamResume.statusCode == 404);
    REQUIRE_FALSE(crossStreamResume.sse.has_value());
  }

  SECTION("Stale Last-Event-ID is rejected with 409")
  {
    mcp_http::StreamableHttpServerOptions options;
    options.http.limits.maxSseBufferedMessages = 1;
    options.http.requireSessionId = true;

    mcp_http::StreamableHttpServer server(options);
    server.upsertSession("session-live", mcp_http::SessionLookupState::kActive, std::string(mcp::kLatestProtocolVersion));

    const mcp_http::ServerResponse opened = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, "session-live"));
    REQUIRE(opened.statusCode == 200);
    REQUIRE(opened.sse.has_value());
    REQUIRE(opened.sse->events.size() == 1);
    REQUIRE(opened.sse->events.front().id.has_value());

    mcp::jsonrpc::Notification first;
    first.method = "notifications/first";
    REQUIRE(server.enqueueServerMessage(mcp::jsonrpc::Message {first}, "session-live"));

    mcp::jsonrpc::Notification second;
    second.method = "notifications/second";
    REQUIRE(server.enqueueServerMessage(mcp::jsonrpc::Message {second}, "session-live"));

    const std::string primingEventId = *opened.sse->events.front().id;
    const mcp_http::ServerResponse staleResume =
      server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, "session-live", primingEventId));

    REQUIRE(staleResume.statusCode == 409);
    REQUIRE_FALSE(staleResume.sse.has_value());
    REQUIRE(staleResume.body.find("retained SSE buffer window") != std::string::npos);
  }
}

TEST_CASE("HTTP server emits SSE priming event with event ID and empty data", "[transport][http][server]")
{
  mcp_http::StreamableHttpServer server;

  const mcp_http::ServerResponse response = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp"));
  REQUIRE(response.statusCode == 200);
  REQUIRE(response.sse.has_value());
  REQUIRE(response.sse->events.size() == 1);

  const mcp::http::sse::Event &priming = response.sse->events.front();
  REQUIRE(priming.id.has_value());
  REQUIRE(priming.data.empty());
  REQUIRE(mcp::http::sse::parseEventId(*priming.id).has_value());
  REQUIRE(response.body.find("data:\n\n") != std::string::npos);
}

TEST_CASE("HTTP server session termination and expiry return 404", "[transport][http][server]")
{
  mcp_http::StreamableHttpServerOptions options;
  options.http.requireSessionId = true;
  options.allowDeleteSession = true;

  mcp_http::StreamableHttpServer server(options);
  server.upsertSession("session-live", mcp_http::SessionLookupState::kActive, std::string(mcp::kLatestProtocolVersion));
  server.upsertSession("session-expired", mcp_http::SessionLookupState::kExpired, std::string(mcp::kLatestProtocolVersion));

  SECTION("Expired session is not accepted")
  {
    const mcp_http::ServerResponse expired = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, "session-expired"));
    REQUIRE(expired.statusCode == 404);
  }

  SECTION("DELETE terminates session and subsequent requests are 404")
  {
    const mcp_http::ServerResponse beforeTerminate = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, "session-live"));
    REQUIRE(beforeTerminate.statusCode == 200);

    const mcp_http::ServerResponse terminated = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kDelete, "/mcp", std::nullopt, "session-live"));
    REQUIRE(terminated.statusCode == 204);

    const mcp_http::ServerResponse afterTerminate = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, "session-live"));
    REQUIRE(afterTerminate.statusCode == 404);
  }
}

TEST_CASE("HTTP server returns 405 when GET SSE is disabled", "[transport][http][server]")
{
  mcp_http::StreamableHttpServerOptions options;
  options.allowGetSse = false;

  mcp_http::StreamableHttpServer server(options);
  const mcp_http::ServerResponse response = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp"));

  REQUIRE(response.statusCode == 405);
}

TEST_CASE("HTTP server rejects POST bodies with missing or invalid Content-Type", "[transport][http][server]")
{
  mcp_http::StreamableHttpServer server;
  const std::string validRequestBody = makeRequestBody(64, "ping");

  SECTION("Missing Content-Type")
  {
    const mcp_http::ServerResponse response =
      server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", validRequestBody, std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    REQUIRE(response.statusCode == 400);
    REQUIRE(response.body.find("Content-Type") != std::string::npos);
  }

  SECTION("Invalid Content-Type")
  {
    const mcp_http::ServerResponse response =
      server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", validRequestBody, std::nullopt, std::nullopt, std::nullopt, "text/plain"));
    REQUIRE(response.statusCode == 400);
    REQUIRE(response.body.find("Content-Type") != std::string::npos);
  }
}

TEST_CASE("HTTP server rejects POST bodies that are not a single JSON-RPC message", "[transport][http][server]")
{
  mcp_http::StreamableHttpServer server;

  SECTION("JSON-RPC batch array is rejected")
  {
    const mcp_http::ServerResponse response =
      server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", R"([{"jsonrpc":"2.0","id":1,"method":"ping"}])"));
    REQUIRE(response.statusCode == 400);
  }

  SECTION("Multiple JSON objects in one POST body are rejected")
  {
    const char *body = R"({"jsonrpc":"2.0","id":1,"method":"ping"}{"jsonrpc":"2.0","id":2,"method":"ping"})";
    const mcp_http::ServerResponse response = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", body));
    REQUIRE(response.statusCode == 400);
  }
}

TEST_CASE("HTTP server supports disconnect and resume without cancelling request", "[transport][http][server]")
{
  mcp_http::StreamableHttpServer server;
  server.setRequestHandler(
    [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &) -> mcp_http::StreamableRequestResult
    {
      mcp_http::StreamableRequestResult result;
      result.useSse = true;
      result.closeSseConnection = true;
      result.retryMilliseconds = 250;

      mcp::jsonrpc::Notification inFlight;
      inFlight.method = "notifications/initialized";
      result.preResponseMessages.push_back(mcp::jsonrpc::Message {inFlight});
      return result;
    });

  const std::string requestBody = makeRequestBody(91, "delayed");
  const mcp_http::ServerResponse firstChunk = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", requestBody));

  REQUIRE(firstChunk.statusCode == 200);
  REQUIRE(firstChunk.sse.has_value());
  REQUIRE_FALSE(firstChunk.sse->terminateStream);
  REQUIRE(firstChunk.body.find("retry: 250") != std::string::npos);

  const std::optional<std::string> cursor = lastEventId(firstChunk.sse->events);
  REQUIRE(cursor.has_value());

  mcp::jsonrpc::SuccessResponse lateResponse;
  lateResponse.id = std::int64_t {91};
  lateResponse.result = mcp::jsonrpc::JsonValue::object();
  lateResponse.result["completed"] = true;
  REQUIRE(server.enqueueServerMessage(mcp::jsonrpc::Message {lateResponse}));

  const mcp_http::ServerResponse resumed = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, std::nullopt, *cursor));

  REQUIRE(resumed.statusCode == 200);
  REQUIRE(resumed.sse.has_value());
  REQUIRE(resumed.sse->terminateStream);
  REQUIRE(resumed.sse->events.size() == 1);

  const mcp::jsonrpc::Message responseMessage = messageFromSseEvent(resumed.sse->events.front());
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(responseMessage));
  REQUIRE(std::get<mcp::jsonrpc::SuccessResponse>(responseMessage).id == mcp::jsonrpc::RequestId {std::int64_t {91}});
}

TEST_CASE("HTTP server returns 401 with OAuth challenge for missing or invalid bearer token", "[transport][http][server][auth]")
{
  auto verifier = std::make_shared<TestTokenVerifier>();
  verifier->setRule("valid-token",
                    {
                      mcp::auth::OAuthTokenVerificationStatus::kValid,
                      true,
                      "principal-a",
                      {"mcp:read"},
                    });

  mcp_http::StreamableHttpServerOptions options;
  options.http.authorization = makeAuthorizationOptions(std::move(verifier));

  mcp_http::StreamableHttpServer server(options);
  server.setRequestHandler(
    [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> mcp_http::StreamableRequestResult
    {
      mcp_http::StreamableRequestResult result;
      mcp::jsonrpc::SuccessResponse response;
      response.id = request.id;
      response.result = mcp::jsonrpc::JsonValue::object();
      response.result["ok"] = true;
      result.response = response;
      return result;
    });

  const std::string body = makeRequestBody(71, "ping");

  SECTION("Missing Authorization header")
  {
    const mcp_http::ServerResponse response = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", body));
    REQUIRE(response.statusCode == 401);
    requireChallengeContains(response, "Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\"");
    requireChallengeContains(response, "scope=\"mcp:read\"");
  }

  SECTION("Unknown bearer token")
  {
    const mcp_http::ServerResponse response =
      server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", body, std::nullopt, std::nullopt, "Bearer unknown-token"));
    REQUIRE(response.statusCode == 401);
    requireChallengeContains(response, "Bearer resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\"");
    requireChallengeContains(response, "scope=\"mcp:read\"");
  }
}

TEST_CASE("HTTP server returns 403 with insufficient_scope challenge", "[transport][http][server][auth]")
{
  auto verifier = std::make_shared<TestTokenVerifier>();
  verifier->setRule("token-read",
                    {
                      mcp::auth::OAuthTokenVerificationStatus::kValid,
                      true,
                      "principal-read",
                      {"mcp:read"},
                    });

  mcp_http::StreamableHttpServerOptions options;
  options.http.authorization = makeAuthorizationOptions(std::move(verifier));
  options.http.authorization->requiredScopesResolver = [](const mcp::auth::OAuthAuthorizationRequestContext &) -> mcp::auth::OAuthScopeSet
  {
    mcp::auth::OAuthScopeSet requiredScopes;
    requiredScopes.values = {"mcp:read", "mcp:write"};
    return requiredScopes;
  };

  mcp_http::StreamableHttpServer server(options);
  const std::string body = makeRequestBody(72, "ping");

  const mcp_http::ServerResponse response =
    server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", body, std::nullopt, std::nullopt, "Bearer token-read"));

  REQUIRE(response.statusCode == 403);
  requireChallengeContains(response, "Bearer error=\"insufficient_scope\"");
  requireChallengeContains(response, "scope=\"mcp:read mcp:write\"");
  requireChallengeContains(response, "resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\"");
}

TEST_CASE("HTTP server rejects tokens without audience binding", "[transport][http][server][auth]")
{
  auto verifier = std::make_shared<TestTokenVerifier>();
  verifier->setRule("token-no-audience",
                    {
                      mcp::auth::OAuthTokenVerificationStatus::kValid,
                      false,
                      "principal-a",
                      {"mcp:read"},
                    });

  mcp_http::StreamableHttpServerOptions options;
  options.http.authorization = makeAuthorizationOptions(std::move(verifier));

  mcp_http::StreamableHttpServer server(options);
  const std::string body = makeRequestBody(73, "ping");

  const mcp_http::ServerResponse response =
    server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", body, std::nullopt, std::nullopt, "Bearer token-no-audience"));

  REQUIRE(response.statusCode == 401);
  requireChallengeContains(response, "resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource/mcp\"");
}

TEST_CASE("HTTP server publishes OAuth protected resource metadata at root and path well-known URIs", "[transport][http][server][auth]")
{
  auto verifier = std::make_shared<TestTokenVerifier>();

  mcp_http::StreamableHttpServerOptions options;
  options.http.endpoint.path = "/public/mcp";
  options.http.authorization = makeAuthorizationOptions(std::move(verifier), "https://mcp.example.com/public/mcp");

  mcp_http::StreamableHttpServer server(options);

  const mcp_http::ServerResponse pathMetadata = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/.well-known/oauth-protected-resource/public/mcp"));
  REQUIRE(pathMetadata.statusCode == 200);

  const mcp::jsonrpc::JsonValue metadata = mcp::jsonrpc::JsonValue::parse(pathMetadata.body);
  REQUIRE(metadata["resource"].as<std::string>() == "https://mcp.example.com/public/mcp");
  REQUIRE(metadata["authorization_servers"].size() == 1);
  REQUIRE(metadata["authorization_servers"][0].as<std::string>() == "https://auth.example.com");
  REQUIRE(metadata["scopes_supported"].size() == 2);

  const mcp_http::ServerResponse rootMetadata = server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kGet, "/.well-known/oauth-protected-resource"));
  REQUIRE(rootMetadata.statusCode == 200);
}

TEST_CASE("HTTP authorization context binds into task isolation through RequestContext", "[transport][http][server][auth][tasks]")
{
  auto verifier = std::make_shared<TestTokenVerifier>();
  verifier->setRule("token-a",
                    {
                      mcp::auth::OAuthTokenVerificationStatus::kValid,
                      true,
                      "principal-a",
                      {"mcp:read"},
                    });
  verifier->setRule("token-b",
                    {
                      mcp::auth::OAuthTokenVerificationStatus::kValid,
                      true,
                      "principal-b",
                      {"mcp:read"},
                    });

  mcp_http::StreamableHttpServerOptions options;
  options.http.authorization = makeAuthorizationOptions(std::move(verifier));

  auto taskStore = std::make_shared<mcp::util::InMemoryTaskStore>();
  mcp::util::TaskReceiver taskReceiver(taskStore);

  mcp_http::StreamableHttpServer server(options);
  server.setRequestHandler(
    [&taskReceiver](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Request &request) -> mcp_http::StreamableRequestResult
    {
      mcp_http::StreamableRequestResult result;

      if (request.method == "test/createTask")
      {
        mcp::util::TaskAugmentationRequest augmentation;
        augmentation.requested = true;
        const mcp::util::CreateTaskResult created = taskReceiver.createTask(context, augmentation);

        mcp::jsonrpc::SuccessResponse response;
        response.id = request.id;
        response.result = mcp::jsonrpc::JsonValue::object();
        response.result["taskId"] = created.task.taskId;
        result.response = response;
        return result;
      }

      if (request.method == "tasks/get")
      {
        result.response = taskReceiver.handleTasksGetRequest(context, request);
        return result;
      }

      result.response = mcp::jsonrpc::makeErrorResponse(mcp::jsonrpc::makeMethodNotFoundError(), request.id);
      return result;
    });

  mcp::jsonrpc::Request createRequest;
  createRequest.id = std::int64_t {81};
  createRequest.method = "test/createTask";
  createRequest.params = mcp::jsonrpc::JsonValue::object();
  const std::string createRequestBody = mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {createRequest});

  const mcp_http::ServerResponse createResponse =
    server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", createRequestBody, std::nullopt, std::nullopt, "Bearer token-a"));
  REQUIRE(createResponse.statusCode == 200);
  const mcp::jsonrpc::Message createResponseMessage = mcp::jsonrpc::parseMessage(createResponse.body);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(createResponseMessage));
  const std::string taskId = std::get<mcp::jsonrpc::SuccessResponse>(createResponseMessage).result["taskId"].as<std::string>();

  mcp::jsonrpc::Request getRequest;
  getRequest.id = std::int64_t {82};
  getRequest.method = "tasks/get";
  getRequest.params = mcp::jsonrpc::JsonValue::object();
  (*getRequest.params)["taskId"] = taskId;
  const std::string getRequestBody = mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {getRequest});

  const mcp_http::ServerResponse deniedResponse =
    server.handleRequest(makeHeaderedRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", getRequestBody, std::nullopt, std::nullopt, "Bearer token-b"));
  REQUIRE(deniedResponse.statusCode == 200);
  const mcp::jsonrpc::Message deniedMessage = mcp::jsonrpc::parseMessage(deniedResponse.body);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(deniedMessage));
  REQUIRE(std::get<mcp::jsonrpc::ErrorResponse>(deniedMessage).error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams));
}

// NOLINTEND(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness)
