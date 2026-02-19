#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/http/sse.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/transport/http.hpp>

// NOLINTBEGIN(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness)

namespace
{

namespace mcp_http = mcp::transport::http;

class LocalHttpServerFixture
{
public:
  mcp_http::StreamableHttpServer server;

  auto execute(const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse
  {
    {
      const std::scoped_lock lock(mutex_);
      requests_.push_back(request);
    }

    if (beforeHandle)
    {
      beforeHandle(request);
    }

    return server.handleRequest(request);
  }

  [[nodiscard]] auto requests() const -> std::vector<mcp_http::ServerRequest>
  {
    const std::scoped_lock lock(mutex_);
    return requests_;
  }

  std::function<void(const mcp_http::ServerRequest &)> beforeHandle;

private:
  mutable std::mutex mutex_;
  std::vector<mcp_http::ServerRequest> requests_;
};

auto makeClientOptions(std::vector<std::uint32_t> *retryDelays = nullptr) -> mcp_http::StreamableHttpClientOptions
{
  mcp_http::StreamableHttpClientOptions options;
  options.endpointUrl = "http://localhost/mcp";
  options.defaultRetryMilliseconds = 10;
  options.waitBeforeReconnect = [retryDelays](std::uint32_t retryMilliseconds) -> void
  {
    if (retryDelays != nullptr)
    {
      retryDelays->push_back(retryMilliseconds);
    }
  };
  return options;
}

auto makeRequest(std::int64_t id, std::string method) -> mcp::jsonrpc::Message
{
  mcp::jsonrpc::Request request;
  request.id = id;
  request.method = std::move(method);
  return mcp::jsonrpc::Message {request};
}

auto makeSuccessResponse(std::int64_t id) -> mcp::jsonrpc::Message
{
  mcp::jsonrpc::SuccessResponse response;
  response.id = id;
  response.result = mcp::jsonrpc::JsonValue::object();
  response.result["ok"] = true;
  return mcp::jsonrpc::Message {response};
}

auto makeNotification(std::string method) -> mcp::jsonrpc::Message
{
  mcp::jsonrpc::Notification notification;
  notification.method = std::move(method);
  return mcp::jsonrpc::Message {notification};
}

auto isNotificationMethod(const mcp::jsonrpc::Message &message, std::string_view method) -> bool
{
  return std::holds_alternative<mcp::jsonrpc::Notification>(message) && std::get<mcp::jsonrpc::Notification>(message).method == method;
}

auto isSuccessResponseFor(const mcp::jsonrpc::Message &message, std::int64_t id) -> bool
{
  return std::holds_alternative<mcp::jsonrpc::SuccessResponse>(message) && std::get<mcp::jsonrpc::SuccessResponse>(message).id == mcp::jsonrpc::RequestId {std::int64_t {id}};
}

auto countPostRequests(const std::vector<mcp_http::ServerRequest> &requests) -> std::size_t
{
  std::size_t count = 0;
  for (const auto &request : requests)
  {
    if (request.method == mcp_http::ServerRequestMethod::kPost)
    {
      ++count;
    }
  }

  return count;
}

auto requireExceptionContains(const std::function<void()> &operation, const std::vector<std::string_view> &expectedSnippets) -> void
{
  try
  {
    operation();
    FAIL("Expected operation to throw an exception.");
  }
  catch (const std::exception &exception)
  {
    const std::string message = exception.what();
    for (const std::string_view snippet : expectedSnippets)
    {
      REQUIRE(message.find(snippet) != std::string::npos);
    }
  }
}

}  // namespace

TEST_CASE("HTTP client sends notifications/responses with POST and handles 202", "[transport][http][client]")
{
  LocalHttpServerFixture fixture;
  mcp_http::StreamableHttpClient client(makeClientOptions(), [&fixture](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse { return fixture.execute(request); });

  const auto notificationResult = client.send(makeNotification("notifications/initialized"));
  REQUIRE(notificationResult.statusCode == 202);
  REQUIRE(notificationResult.messages.empty());
  REQUIRE_FALSE(notificationResult.response.has_value());

  const auto responseResult = client.send(makeSuccessResponse(7));
  REQUIRE(responseResult.statusCode == 202);
  REQUIRE(responseResult.messages.empty());
  REQUIRE_FALSE(responseResult.response.has_value());

  const auto requests = fixture.requests();
  REQUIRE(requests.size() == 2);

  for (const auto &request : requests)
  {
    REQUIRE(request.method == mcp_http::ServerRequestMethod::kPost);
    REQUIRE(mcp_http::getHeader(request.headers, mcp_http::kHeaderAccept) == std::optional<std::string> {"application/json, text/event-stream"});
    REQUIRE(mcp_http::getHeader(request.headers, mcp_http::kHeaderContentType) == std::optional<std::string> {"application/json"});
  }
}

TEST_CASE("HTTP client throws when JSON body is returned with unsupported content type", "[transport][http][client]")
{
  mcp_http::StreamableHttpClient client(makeClientOptions(),
                                        [](const mcp_http::ServerRequest &) -> mcp_http::ServerResponse
                                        {
                                          mcp_http::ServerResponse response;
                                          response.statusCode = 200;
                                          mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "text/plain");
                                          response.body = mcp::jsonrpc::serializeMessage(makeSuccessResponse(11));
                                          return response;
                                        });

  requireExceptionContains([&client]() -> void { static_cast<void>(client.send(makeRequest(11, "tools/list"))); }, {"content type", "application/json", "text/event-stream"});
}

TEST_CASE("HTTP client rejects malformed SSE payloads", "[transport][http][client]")
{
  mcp_http::StreamableHttpClient client(makeClientOptions(),
                                        [](const mcp_http::ServerRequest &) -> mcp_http::ServerResponse
                                        {
                                          mcp_http::ServerResponse response;
                                          response.statusCode = 200;
                                          mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "text/event-stream");
                                          response.body = "data: {not-json}\n\n";
                                          return response;
                                        });

  requireExceptionContains([&client]() -> void { static_cast<void>(client.send(makeRequest(12, "tools/list"))); }, {"Failed to parse JSON-RPC message"});
}

TEST_CASE("HTTP client surfaces actionable 404 error for stale session requests", "[transport][http][client]")
{
  std::vector<mcp_http::ServerRequest> observedRequests;

  mcp_http::StreamableHttpClientOptions options = makeClientOptions();
  REQUIRE(options.sessionState.captureFromInitializeResponse("stale-session"));

  mcp_http::StreamableHttpClient client(std::move(options),
                                        [&observedRequests](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse
                                        {
                                          observedRequests.push_back(request);

                                          mcp_http::ServerResponse response;
                                          response.statusCode = 404;
                                          return response;
                                        });

  requireExceptionContains([&client]() -> void { static_cast<void>(client.send(makeRequest(13, "initialize"))); }, {"404", "expected 200"});

  REQUIRE(observedRequests.size() == 1);
  REQUIRE(mcp_http::getHeader(observedRequests.front().headers, mcp_http::kHeaderMcpSessionId) == std::optional<std::string> {"stale-session"});
}

TEST_CASE("HTTP client reconnects with GET Last-Event-ID and respects retry guidance", "[transport][http][client]")
{
  LocalHttpServerFixture fixture;
  fixture.server.setRequestHandler(
    [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &) -> mcp_http::StreamableRequestResult
    {
      mcp_http::StreamableRequestResult result;
      result.useSse = true;
      result.closeSseConnection = true;
      result.retryMilliseconds = 37;
      result.preResponseMessages.push_back(makeNotification("notifications/customProgress"));
      return result;
    });

  bool queuedFinalResponse = false;
  fixture.beforeHandle = [&fixture, &queuedFinalResponse](const mcp_http::ServerRequest &request) -> void
  {
    const auto lastEventId = mcp_http::getHeader(request.headers, mcp_http::kHeaderLastEventId);
    if (!queuedFinalResponse && request.method == mcp_http::ServerRequestMethod::kGet && lastEventId.has_value())
    {
      REQUIRE(fixture.server.enqueueServerMessage(makeSuccessResponse(91)));
      queuedFinalResponse = true;
    }
  };

  std::vector<std::uint32_t> retryDelays;
  mcp_http::StreamableHttpClient client(makeClientOptions(&retryDelays),
                                        [&fixture](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse { return fixture.execute(request); });

  const auto result = client.send(makeRequest(91, "longRunning"));

  REQUIRE(result.response.has_value());
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(*result.response));
  REQUIRE(std::get<mcp::jsonrpc::SuccessResponse>(*result.response).id == mcp::jsonrpc::RequestId {std::int64_t {91}});
  REQUIRE(result.messages.size() == 1);
  REQUIRE(isNotificationMethod(result.messages.front(), "notifications/customProgress"));
  REQUIRE(retryDelays == std::vector<std::uint32_t> {37});

  const auto requests = fixture.requests();
  REQUIRE(requests.size() >= 2);
  REQUIRE(requests.front().method == mcp_http::ServerRequestMethod::kPost);
  REQUIRE(countPostRequests(requests) == 1);

  bool observedResumeGet = false;
  for (const auto &request : requests)
  {
    if (request.method != mcp_http::ServerRequestMethod::kGet)
    {
      continue;
    }

    const auto lastEventId = mcp_http::getHeader(request.headers, mcp_http::kHeaderLastEventId);
    if (!lastEventId.has_value())
    {
      continue;
    }

    observedResumeGet = true;
    REQUIRE(mcp_http::getHeader(request.headers, mcp_http::kHeaderAccept) == std::optional<std::string> {"text/event-stream"});
    REQUIRE(mcp::http::sse::parseEventId(*lastEventId).has_value());
  }

  REQUIRE(observedResumeGet);
}

TEST_CASE("HTTP client supports GET listen stream alongside POST SSE resume without duplicates", "[transport][http][client]")
{
  LocalHttpServerFixture fixture;
  fixture.server.setRequestHandler(
    [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &) -> mcp_http::StreamableRequestResult
    {
      mcp_http::StreamableRequestResult result;
      result.useSse = true;
      result.closeSseConnection = true;
      result.retryMilliseconds = 12;
      return result;
    });

  bool sawPost = false;
  bool queuedPostResponse = false;
  fixture.beforeHandle = [&fixture, &sawPost, &queuedPostResponse](const mcp_http::ServerRequest &request) -> void
  {
    if (request.method == mcp_http::ServerRequestMethod::kPost)
    {
      sawPost = true;
      return;
    }

    const auto lastEventId = mcp_http::getHeader(request.headers, mcp_http::kHeaderLastEventId);
    if (request.method == mcp_http::ServerRequestMethod::kGet && sawPost && !queuedPostResponse && lastEventId.has_value())
    {
      REQUIRE(fixture.server.enqueueServerMessage(makeSuccessResponse(17)));
      queuedPostResponse = true;
    }
  };

  std::vector<std::uint32_t> retryDelays;
  mcp_http::StreamableHttpClient client(makeClientOptions(&retryDelays),
                                        [&fixture](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse { return fixture.execute(request); });

  const auto opened = client.openListenStream();
  REQUIRE(opened.statusCode == 200);
  REQUIRE(opened.streamOpen);
  REQUIRE(opened.messages.empty());

  REQUIRE(fixture.server.enqueueServerMessage(makeNotification("notifications/listen")));
  const auto firstListenPoll = client.pollListenStream();
  REQUIRE(firstListenPoll.statusCode == 200);
  REQUIRE(firstListenPoll.streamOpen);
  REQUIRE(firstListenPoll.messages.size() == 1);
  REQUIRE(isNotificationMethod(firstListenPoll.messages.front(), "notifications/listen"));

  const auto sendResult = client.send(makeRequest(17, "work"));
  REQUIRE(sendResult.response.has_value());
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(*sendResult.response));
  REQUIRE(std::get<mcp::jsonrpc::SuccessResponse>(*sendResult.response).id == mcp::jsonrpc::RequestId {std::int64_t {17}});

  const auto secondListenPoll = client.pollListenStream();
  REQUIRE(secondListenPoll.statusCode == 200);

  std::size_t listenNotificationCount = 0;
  for (const auto &message : firstListenPoll.messages)
  {
    if (isNotificationMethod(message, "notifications/listen"))
    {
      ++listenNotificationCount;
    }
  }

  for (const auto &message : sendResult.messages)
  {
    if (isNotificationMethod(message, "notifications/listen"))
    {
      ++listenNotificationCount;
    }
  }

  for (const auto &message : secondListenPoll.messages)
  {
    if (isNotificationMethod(message, "notifications/listen"))
    {
      ++listenNotificationCount;
    }

    REQUIRE_FALSE(isSuccessResponseFor(message, 17));
  }

  REQUIRE(listenNotificationCount == 1);

  std::size_t responseCount = 0;
  if (sendResult.response.has_value() && std::holds_alternative<mcp::jsonrpc::SuccessResponse>(*sendResult.response)
      && std::get<mcp::jsonrpc::SuccessResponse>(*sendResult.response).id == mcp::jsonrpc::RequestId {std::int64_t {17}})
  {
    ++responseCount;
  }

  for (const auto &message : sendResult.messages)
  {
    if (isSuccessResponseFor(message, 17))
    {
      ++responseCount;
    }
  }

  for (const auto &message : firstListenPoll.messages)
  {
    if (isSuccessResponseFor(message, 17))
    {
      ++responseCount;
    }
  }

  for (const auto &message : secondListenPoll.messages)
  {
    if (isSuccessResponseFor(message, 17))
    {
      ++responseCount;
    }
  }

  REQUIRE(responseCount == 1);

  const auto requests = fixture.requests();
  std::set<std::string> resumedStreamIds;
  for (const auto &request : requests)
  {
    if (request.method != mcp_http::ServerRequestMethod::kGet)
    {
      continue;
    }

    const auto lastEventId = mcp_http::getHeader(request.headers, mcp_http::kHeaderLastEventId);
    if (!lastEventId.has_value())
    {
      continue;
    }

    const auto parsed = mcp::http::sse::parseEventId(*lastEventId);
    REQUIRE(parsed.has_value());
    resumedStreamIds.insert(parsed->streamId);
  }

  REQUIRE(resumedStreamIds.size() >= 2);
}

TEST_CASE("HTTP client legacy fallback normalizes absolute endpoint targets", "[transport][http][client]")
{
  mcp_http::StreamableHttpClientOptions options = makeClientOptions();
  options.endpointUrl = "http://localhost/mcp";
  options.enableLegacyHttpSseFallback = true;
  options.legacyFallbackSsePath = "http://localhost/events#ignored";
  options.legacyFallbackPostPath = "http://localhost:80?mode=legacy#fragment";

  std::vector<mcp_http::ServerRequest> observedRequests;
  mcp_http::StreamableHttpClient client(std::move(options),
                                        [&observedRequests](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse
                                        {
                                          observedRequests.push_back(request);

                                          if (observedRequests.size() == 1)
                                          {
                                            mcp_http::ServerResponse response;
                                            response.statusCode = 404;
                                            return response;
                                          }

                                          if (observedRequests.size() == 2)
                                          {
                                            REQUIRE(request.method == mcp_http::ServerRequestMethod::kGet);
                                            REQUIRE(request.path == "/events");

                                            mcp::http::sse::Event endpointEvent;
                                            endpointEvent.event = "endpoint";
                                            endpointEvent.data = "";

                                            mcp_http::ServerResponse response;
                                            response.statusCode = 200;
                                            mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "text/event-stream");
                                            response.body = mcp::http::sse::encodeEvents({endpointEvent});
                                            return response;
                                          }

                                          if (observedRequests.size() == 3)
                                          {
                                            REQUIRE(request.method == mcp_http::ServerRequestMethod::kPost);
                                            REQUIRE(request.path == "/?mode=legacy");

                                            mcp_http::ServerResponse response;
                                            response.statusCode = 200;
                                            mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "application/json");
                                            response.body = mcp::jsonrpc::serializeMessage(makeSuccessResponse(501));
                                            return response;
                                          }

                                          FAIL("Unexpected request in legacy fallback endpoint normalization test.");
                                          return {};
                                        });

  const mcp_http::StreamableHttpSendResult result = client.send(makeRequest(501, "initialize"));
  REQUIRE(result.statusCode == 200);
  REQUIRE(result.response.has_value());
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(*result.response));
  REQUIRE(std::get<mcp::jsonrpc::SuccessResponse>(*result.response).id == mcp::jsonrpc::RequestId {std::int64_t {501}});
}

TEST_CASE("HTTP client legacy fallback rejects userinfo in absolute endpoint targets", "[transport][http][client]")
{
  mcp_http::StreamableHttpClientOptions options = makeClientOptions();
  options.endpointUrl = "http://localhost/mcp";
  options.enableLegacyHttpSseFallback = true;
  options.legacyFallbackPostPath = "http://user@localhost/rpc";

  std::vector<mcp_http::ServerRequest> observedRequests;
  mcp_http::StreamableHttpClient client(std::move(options),
                                        [&observedRequests](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse
                                        {
                                          observedRequests.push_back(request);
                                          mcp_http::ServerResponse response;
                                          response.statusCode = 404;
                                          return response;
                                        });

  requireExceptionContains([&client]() -> void { static_cast<void>(client.send(makeRequest(502, "initialize"))); }, {"absolute path", "same-origin"});
  REQUIRE(observedRequests.size() == 1);
}

// NOLINTEND(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness)
