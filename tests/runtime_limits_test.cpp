#include <chrono>
#include <future>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/sdk/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/transport/stdio.hpp>
#include <mcp/util/tasks.hpp>

namespace
{

namespace mcp_http = mcp::transport::http;

auto makeReadyResponseFuture(mcp::jsonrpc::Response response) -> std::future<mcp::jsonrpc::Response>
{
  std::promise<mcp::jsonrpc::Response> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

auto makeRequestMessage(std::int64_t id, std::string method = "ping") -> mcp::jsonrpc::Message
{
  mcp::jsonrpc::Request request;
  request.id = id;
  request.method = std::move(method);
  return mcp::jsonrpc::Message {request};
}

auto makeServerRequest(mcp_http::ServerRequestMethod method,
                       std::string path,
                       std::optional<std::string> body = std::nullopt,
                       std::optional<std::string> lastEventId = std::nullopt) -> mcp_http::ServerRequest
{
  mcp_http::ServerRequest request;
  request.method = method;
  request.path = std::move(path);
  if (body.has_value())
  {
    request.body = std::move(*body);
    mcp_http::setHeader(request.headers, mcp_http::kHeaderContentType, "application/json");
  }

  if (lastEventId.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderLastEventId, std::move(*lastEventId));
  }

  return request;
}

auto contains(std::string_view text, std::string_view fragment) -> bool
{
  return text.find(fragment) != std::string_view::npos;
}

}  // namespace

TEST_CASE("Stdio transport enforces max message size and emits parse error", "[limits][transport][stdio]")
{
  mcp::jsonrpc::Router router;
  router.registerRequestHandler("ping",
                                [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                {
                                  mcp::jsonrpc::SuccessResponse response;
                                  response.id = request.id;
                                  response.result = mcp::jsonrpc::JsonValue::object();
                                  return makeReadyResponseFuture(mcp::jsonrpc::Response {response});
                                });

  mcp::transport::StdioAttachOptions options;
  options.allowStderrLogs = true;
  options.emitParseErrors = true;
  options.limits.maxMessageSizeBytes = 32;

  const std::string oversizedLine = R"({"jsonrpc":"2.0","id":1,"method":"ping","params":{"payload":"abcdefghijklmnopqrstuvwxyz"}})";

  std::ostringstream output;
  std::ostringstream diagnostics;
  REQUIRE_FALSE(mcp::transport::StdioTransport::routeIncomingLine(router, oversizedLine, output, &diagnostics, options));
  REQUIRE(contains(diagnostics.str(), "max message size"));

  std::string parseErrorLine;
  std::istringstream outputReader(output.str());
  REQUIRE(std::getline(outputReader, parseErrorLine));
  const mcp::jsonrpc::Message parseErrorMessage = mcp::jsonrpc::parseMessage(parseErrorLine);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(parseErrorMessage));
  REQUIRE(std::get<mcp::jsonrpc::ErrorResponse>(parseErrorMessage).error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kParseError));
}

TEST_CASE("HTTP server enforces max JSON body size", "[limits][transport][http]")
{
  mcp_http::StreamableHttpServerOptions options;
  options.http.limits.maxMessageSizeBytes = 32;
  mcp_http::StreamableHttpServer server(options);

  const mcp_http::ServerResponse response = server.handleRequest(
    makeServerRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", std::string(R"({"jsonrpc":"2.0","id":1,"method":"ping","params":{"payload":"abcdefghijklmnopqrstuvwxyz"}})")));

  REQUIRE(response.statusCode == 400);
  REQUIRE(contains(response.body, "max message size"));
}

TEST_CASE("Router enforces max concurrent in-flight requests", "[limits][router]")
{
  SECTION("Outbound requests")
  {
    mcp::jsonrpc::RouterOptions options;
    options.maxConcurrentInFlightRequests = 1;
    mcp::jsonrpc::Router router(options);
    router.setOutboundMessageSender([](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Message &) -> void {});

    const mcp::jsonrpc::RequestContext context;
    mcp::jsonrpc::Request firstRequest;
    firstRequest.id = std::int64_t {1};
    firstRequest.method = "tools/call";

    mcp::jsonrpc::Request secondRequest;
    secondRequest.id = std::int64_t {2};
    secondRequest.method = "tools/call";

    std::future<mcp::jsonrpc::Response> firstFuture = router.sendRequest(context, firstRequest);
    const mcp::jsonrpc::Response secondResponse = router.sendRequest(context, secondRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(secondResponse));
    REQUIRE(contains(std::get<mcp::jsonrpc::ErrorResponse>(secondResponse).error.message, "max concurrent in-flight outbound requests"));

    mcp::jsonrpc::SuccessResponse completion;
    completion.id = std::int64_t {1};
    completion.result = mcp::jsonrpc::JsonValue::object();
    REQUIRE(router.dispatchResponse(context, mcp::jsonrpc::Response {completion}));
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(firstFuture.get()));
  }

  SECTION("Inbound requests")
  {
    mcp::jsonrpc::RouterOptions options;
    options.maxConcurrentInFlightRequests = 1;
    mcp::jsonrpc::Router router(options);

    std::promise<void> unblock;
    std::shared_future<void> unblockSignal = unblock.get_future().share();
    router.registerRequestHandler("tools/call",
                                  [unblockSignal](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                  {
                                    return std::async(std::launch::async,
                                                      [unblockSignal, request]() -> mcp::jsonrpc::Response
                                                      {
                                                        unblockSignal.wait();
                                                        mcp::jsonrpc::SuccessResponse response;
                                                        response.id = request.id;
                                                        response.result = mcp::jsonrpc::JsonValue::object();
                                                        return mcp::jsonrpc::Response {response};
                                                      });
                                  });

    const mcp::jsonrpc::RequestContext context;
    mcp::jsonrpc::Request firstRequest;
    firstRequest.id = std::int64_t {11};
    firstRequest.method = "tools/call";

    mcp::jsonrpc::Request secondRequest;
    secondRequest.id = std::int64_t {12};
    secondRequest.method = "tools/call";

    std::future<mcp::jsonrpc::Response> firstResponse = router.dispatchRequest(context, firstRequest);
    const mcp::jsonrpc::Response secondResponse = router.dispatchRequest(context, secondRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(secondResponse));
    REQUIRE(contains(std::get<mcp::jsonrpc::ErrorResponse>(secondResponse).error.message, "max concurrent in-flight inbound requests"));

    unblock.set_value();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(firstResponse.get()));
  }
}

TEST_CASE("HTTP SSE buffering and stream duration limits are enforced", "[limits][transport][http][sse]")
{
  SECTION("Dropped buffered events reject stale Last-Event-ID")
  {
    mcp_http::StreamableHttpServerOptions options;
    options.http.limits.maxSseBufferedMessages = 2;
    mcp_http::StreamableHttpServer server(options);

    const mcp_http::ServerResponse opened = server.handleRequest(makeServerRequest(mcp_http::ServerRequestMethod::kGet, "/mcp"));
    REQUIRE(opened.statusCode == 200);
    REQUIRE(opened.sse.has_value());
    REQUIRE(opened.sse->events.size() == 1);
    REQUIRE(opened.sse->events.front().id.has_value());
    const std::string firstEventId = *opened.sse->events.front().id;

    mcp::jsonrpc::Notification messageA;
    messageA.method = "notifications/a";
    REQUIRE(server.enqueueServerMessage(mcp::jsonrpc::Message {messageA}));

    mcp::jsonrpc::Notification messageB;
    messageB.method = "notifications/b";
    REQUIRE(server.enqueueServerMessage(mcp::jsonrpc::Message {messageB}));

    mcp::jsonrpc::Notification messageC;
    messageC.method = "notifications/c";
    REQUIRE(server.enqueueServerMessage(mcp::jsonrpc::Message {messageC}));

    const mcp_http::ServerResponse staleResume = server.handleRequest(makeServerRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, firstEventId));
    REQUIRE(staleResume.statusCode == 409);
    REQUIRE(contains(staleResume.body, "retained SSE buffer"));
  }

  SECTION("Pending queue applies backpressure")
  {
    mcp_http::StreamableHttpServerOptions options;
    options.http.limits.maxSseBufferedMessages = 1;
    mcp_http::StreamableHttpServer server(options);

    mcp::jsonrpc::Notification first;
    first.method = "notifications/first";
    REQUIRE(server.enqueueServerMessage(mcp::jsonrpc::Message {first}));

    mcp::jsonrpc::Notification second;
    second.method = "notifications/second";
    REQUIRE_FALSE(server.enqueueServerMessage(mcp::jsonrpc::Message {second}));
  }

  SECTION("Stream duration expiration terminates resumability")
  {
    mcp_http::StreamableHttpServerOptions options;
    options.http.limits.maxSseStreamDuration = std::chrono::milliseconds(1);
    mcp_http::StreamableHttpServer server(options);

    const mcp_http::ServerResponse opened = server.handleRequest(makeServerRequest(mcp_http::ServerRequestMethod::kGet, "/mcp"));
    REQUIRE(opened.statusCode == 200);
    REQUIRE(opened.sse.has_value());
    REQUIRE(opened.sse->events.front().id.has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    const mcp_http::ServerResponse expiredResume =
      server.handleRequest(makeServerRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, *opened.sse->events.front().id));
    REQUIRE(expiredResume.statusCode == 404);
  }
}

TEST_CASE("HTTP client enforces retry and payload limits", "[limits][transport][http][client]")
{
  SECTION("Reconnect attempts are capped")
  {
    mcp_http::StreamableHttpServer server;
    server.setRequestHandler(
      [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &) -> mcp_http::StreamableRequestResult
      {
        mcp_http::StreamableRequestResult result;
        result.useSse = true;
        result.closeSseConnection = true;
        result.retryMilliseconds = 1;
        return result;
      });

    std::vector<std::uint32_t> reconnectDelays;
    mcp_http::StreamableHttpClientOptions options;
    options.endpointUrl = "http://localhost/mcp";
    options.limits.maxRetryAttempts = 2;
    options.waitBeforeReconnect = [&reconnectDelays](std::uint32_t retryMilliseconds) -> void { reconnectDelays.push_back(retryMilliseconds); };

    mcp_http::StreamableHttpClient client(options, [&server](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse { return server.handleRequest(request); });

    bool threw = false;
    try
    {
      static_cast<void>(client.send(makeRequestMessage(77, "work")));
    }
    catch (const std::runtime_error &error)
    {
      threw = true;
      REQUIRE(contains(error.what(), "retry attempts"));
    }

    REQUIRE(threw);
    REQUIRE(reconnectDelays.size() == 2);
  }

  SECTION("SSE retry hint is clamped by max retry delay")
  {
    mcp_http::StreamableHttpServer server;
    server.setRequestHandler(
      [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &) -> mcp_http::StreamableRequestResult
      {
        mcp_http::StreamableRequestResult result;
        result.useSse = true;
        result.closeSseConnection = true;
        result.retryMilliseconds = 250;
        return result;
      });

    std::vector<std::uint32_t> reconnectDelays;
    mcp_http::StreamableHttpClientOptions options;
    options.endpointUrl = "http://localhost/mcp";
    options.limits.maxRetryAttempts = 2;
    options.limits.maxRetryDelayMilliseconds = 7;
    options.waitBeforeReconnect = [&reconnectDelays](std::uint32_t retryMilliseconds) -> void { reconnectDelays.push_back(retryMilliseconds); };

    mcp_http::StreamableHttpClient client(options, [&server](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse { return server.handleRequest(request); });

    bool threw = false;
    try
    {
      static_cast<void>(client.send(makeRequestMessage(78, "work")));
    }
    catch (const std::runtime_error &error)
    {
      threw = true;
      REQUIRE(contains(error.what(), "retry attempts"));
    }

    REQUIRE(threw);
    REQUIRE(reconnectDelays.size() == 2);
    for (const std::uint32_t delay : reconnectDelays)
    {
      REQUIRE(delay == 7);
    }
  }

  SECTION("Default retry delay is clamped by max retry delay")
  {
    mcp_http::StreamableHttpServer server;
    server.setRequestHandler(
      [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &) -> mcp_http::StreamableRequestResult
      {
        mcp_http::StreamableRequestResult result;
        result.useSse = true;
        result.closeSseConnection = true;
        return result;
      });

    std::vector<std::uint32_t> reconnectDelays;
    mcp_http::StreamableHttpClientOptions options;
    options.endpointUrl = "http://localhost/mcp";
    options.defaultRetryMilliseconds = 250;
    options.limits.maxRetryAttempts = 2;
    options.limits.maxRetryDelayMilliseconds = 11;
    options.waitBeforeReconnect = [&reconnectDelays](std::uint32_t retryMilliseconds) -> void { reconnectDelays.push_back(retryMilliseconds); };

    mcp_http::StreamableHttpClient client(options, [&server](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse { return server.handleRequest(request); });

    bool threw = false;
    try
    {
      static_cast<void>(client.send(makeRequestMessage(79, "work")));
    }
    catch (const std::runtime_error &error)
    {
      threw = true;
      REQUIRE(contains(error.what(), "retry attempts"));
    }

    REQUIRE(threw);
    REQUIRE(reconnectDelays.size() == 2);
    for (const std::uint32_t delay : reconnectDelays)
    {
      REQUIRE(delay == 11);
    }
  }

  SECTION("JSON payload size cap is enforced")
  {
    mcp_http::StreamableHttpClientOptions options;
    options.endpointUrl = "http://localhost/mcp";
    options.limits.maxMessageSizeBytes = 48;

    mcp_http::StreamableHttpClient client(options,
                                          [](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse
                                          {
                                            static_cast<void>(request);
                                            mcp_http::ServerResponse response;
                                            response.statusCode = 200;
                                            mcp_http::setHeader(response.headers, mcp_http::kHeaderContentType, "application/json");

                                            mcp::jsonrpc::SuccessResponse success;
                                            success.id = std::int64_t {99};
                                            success.result = mcp::jsonrpc::JsonValue::object();
                                            success.result["payload"] = "abcdefghijklmnopqrstuvwxyz0123456789";
                                            response.body = mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {success});
                                            return response;
                                          });

    bool threw = false;
    try
    {
      static_cast<void>(client.send(makeRequestMessage(99, "work")));
    }
    catch (const std::runtime_error &error)
    {
      threw = true;
      REQUIRE(contains(error.what(), "max message size"));
    }

    REQUIRE(threw);
  }
}

TEST_CASE("Task store enforces ttl and per-auth-context concurrency limits", "[limits][tasks]")
{
  mcp::util::InMemoryTaskStoreOptions options;
  options.maxTaskTtlMilliseconds = 50;
  options.maxActiveTasksPerAuthContext = 1;
  mcp::util::InMemoryTaskStore store(options);

  mcp::util::TaskCreateOptions highTtl;
  highTtl.authContext = "auth-a";
  highTtl.ttl = 100;
  const mcp::util::TaskRecordResult highTtlResult = store.createTask(highTtl);
  REQUIRE(highTtlResult.error == mcp::util::TaskStoreError::kLimitExceeded);
  REQUIRE(highTtlResult.errorMessage.has_value());
  REQUIRE(contains(*highTtlResult.errorMessage, "ttl"));

  mcp::util::TaskCreateOptions first;
  first.authContext = "auth-a";
  first.ttl = 10;
  const mcp::util::TaskRecordResult firstResult = store.createTask(first);
  REQUIRE(firstResult.error == mcp::util::TaskStoreError::kNone);

  mcp::util::TaskCreateOptions second;
  second.authContext = "auth-a";
  const mcp::util::TaskRecordResult secondResult = store.createTask(second);
  REQUIRE(secondResult.error == mcp::util::TaskStoreError::kLimitExceeded);
  REQUIRE(secondResult.errorMessage.has_value());
  REQUIRE(contains(*secondResult.errorMessage, "Active task limit"));

  mcp::jsonrpc::JsonValue completionPayload = mcp::jsonrpc::JsonValue::object();
  completionPayload["ok"] = true;
  REQUIRE(store.setTaskResult(firstResult.task.taskId, mcp::util::TaskStatus::kCompleted, std::nullopt, completionPayload, first.authContext).error
          == mcp::util::TaskStoreError::kNone);

  const mcp::util::TaskRecordResult afterTerminal = store.createTask(second);
  REQUIRE(afterTerminal.error == mcp::util::TaskStoreError::kNone);

  auto sharedStore = std::make_shared<mcp::util::InMemoryTaskStore>(options);
  mcp::util::TaskReceiver receiver(sharedStore);
  mcp::jsonrpc::RequestContext context;
  context.authContext = "auth-a";

  mcp::util::TaskAugmentationRequest augmentation;
  augmentation.requested = true;
  augmentation.ttlProvided = true;
  augmentation.ttl = 500;

  bool threw = false;
  try
  {
    static_cast<void>(receiver.createTask(context, augmentation));
  }
  catch (const std::runtime_error &error)
  {
    threw = true;
    REQUIRE(contains(error.what(), "ttl"));
  }

  REQUIRE(threw);
}
