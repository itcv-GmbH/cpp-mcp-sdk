#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <mcp/http/sse.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/transport/http.hpp>

// NOLINTBEGIN(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness)

namespace
{

namespace mcp_http = mcp::transport::http;

auto makeHeaderedRequest(mcp_http::ServerRequestMethod method,
                         std::string path,
                         std::optional<std::string_view> body = std::nullopt,
                         std::optional<std::string_view> sessionId = std::nullopt,
                         std::optional<std::string_view> lastEventId = std::nullopt) -> mcp_http::ServerRequest
{
  mcp_http::ServerRequest request;
  request.method = method;
  request.path = std::move(path);

  if (body.has_value())
  {
    request.body = std::string(*body);
  }

  if (sessionId.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderMcpSessionId, std::string(*sessionId));
  }

  if (lastEventId.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderLastEventId, std::string(*lastEventId));
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

// NOLINTEND(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness)
