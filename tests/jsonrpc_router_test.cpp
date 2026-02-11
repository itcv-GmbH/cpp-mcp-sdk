#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mcp/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>

static constexpr std::int64_t kDuplicateRequestId = 7;
static constexpr std::int64_t kSharedRequestIdAcrossSenders = 11;
static constexpr std::int64_t kOutboundRequestIdA = 101;
static constexpr std::int64_t kOutboundRequestIdB = 202;
static constexpr std::int64_t kTimeoutRequestId = 303;
static constexpr std::int64_t kProgressRequestId = 404;
static constexpr std::int64_t kUnknownResponseId = 999;
static constexpr std::int64_t kInitializeRequestId = 808;
static constexpr std::int64_t kInboundProgressRequestId = 5150;
static constexpr std::int64_t kTaskTimeoutRequestId = 6160;

static constexpr double kProgressQuarter = 0.25;
static constexpr double kProgressLate = 0.8;
static constexpr double kProgressHalf = 0.5;

static constexpr std::int64_t kWaitPollAttempts = 40;
static constexpr std::int64_t kWaitPollMillis = 5;
static constexpr std::int64_t kRequestTimeoutMillis = 30;
static constexpr std::int64_t kInitializeTimeoutMillis = 20;
static constexpr std::int64_t kResponseWaitMillis = 500;
static constexpr std::int64_t kInitializePostTimeoutWaitMillis = 30;

static auto hasIdValue(const mcp::jsonrpc::RequestId &id, std::int64_t expectedValue) -> bool
{
  if (!std::holds_alternative<std::int64_t>(id))
  {
    return false;
  }

  return std::get<std::int64_t>(id) == expectedValue;
}

static auto makeReadyResponseFuture(mcp::jsonrpc::Response response) -> std::future<mcp::jsonrpc::Response>
{
  std::promise<mcp::jsonrpc::Response> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

static auto waitForMessageCount(const std::vector<mcp::jsonrpc::Message> &messages, std::mutex &messagesMutex, std::size_t expectedCount) -> void
{
  for (std::int64_t attempt = 0; attempt < kWaitPollAttempts; ++attempt)
  {
    {
      const std::scoped_lock lock(messagesMutex);
      if (messages.size() >= expectedCount)
      {
        return;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitPollMillis));
  }
}

static auto copyMessages(const std::vector<mcp::jsonrpc::Message> &messages, std::mutex &messagesMutex) -> std::vector<mcp::jsonrpc::Message>
{
  const std::scoped_lock lock(messagesMutex);
  return messages;
}

static auto messageCount(const std::vector<mcp::jsonrpc::Message> &messages, std::mutex &messagesMutex) -> std::size_t
{
  const std::scoped_lock lock(messagesMutex);
  return messages.size();
}

static auto recordMessage(std::vector<mcp::jsonrpc::Message> &messages, std::mutex &messagesMutex, mcp::jsonrpc::Message message) -> void
{
  const std::scoped_lock lock(messagesMutex);
  messages.push_back(std::move(message));
}

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Router enforces unique request ids per sender", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  router.registerRequestHandler("ping",
                                [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                {
                                  mcp::jsonrpc::SuccessResponse success;
                                  success.id = request.id;
                                  success.result = mcp::jsonrpc::JsonValue::object();
                                  return makeReadyResponseFuture(mcp::jsonrpc::Response {success});
                                });

  mcp::jsonrpc::Request request;
  request.id = kDuplicateRequestId;
  request.method = "ping";

  mcp::jsonrpc::RequestContext sender;
  sender.sessionId = "session-a";

  const mcp::jsonrpc::Response firstResponse = router.dispatchRequest(sender, request).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(firstResponse));

  const mcp::jsonrpc::Response duplicateResponse = router.dispatchRequest(sender, request).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(duplicateResponse));

  const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(duplicateResponse);
  REQUIRE(error.id.has_value());
  if (error.id.has_value())
  {
    REQUIRE(hasIdValue(*error.id, kDuplicateRequestId));
  }
  REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidRequest));
}

TEST_CASE("Router allows same request id from different senders", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  router.registerRequestHandler("ping",
                                [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                {
                                  mcp::jsonrpc::SuccessResponse success;
                                  success.id = request.id;
                                  success.result = mcp::jsonrpc::JsonValue::object();
                                  return makeReadyResponseFuture(mcp::jsonrpc::Response {success});
                                });

  mcp::jsonrpc::Request request;
  request.id = kSharedRequestIdAcrossSenders;
  request.method = "ping";

  mcp::jsonrpc::RequestContext senderA;
  senderA.sessionId = "session-a";

  mcp::jsonrpc::RequestContext senderB;
  senderB.sessionId = "session-b";

  const mcp::jsonrpc::Response responseA = router.dispatchRequest(senderA, request).get();
  const mcp::jsonrpc::Response responseB = router.dispatchRequest(senderB, request).get();

  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(responseA));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(responseB));
}

TEST_CASE("Router correlates concurrent outbound responses to the correct waiters", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  std::mutex outboundMessagesMutex;
  std::vector<mcp::jsonrpc::Message> outboundMessages;
  router.setOutboundMessageSender([&outboundMessages, &outboundMessagesMutex](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
                                  { recordMessage(outboundMessages, outboundMessagesMutex, std::move(message)); });

  const mcp::jsonrpc::RequestContext context;

  mcp::jsonrpc::Request firstRequest;
  firstRequest.id = kOutboundRequestIdA;
  firstRequest.method = "tools/call";
  firstRequest.params = mcp::jsonrpc::JsonValue::object();

  mcp::jsonrpc::Request secondRequest;
  secondRequest.id = kOutboundRequestIdB;
  secondRequest.method = "resources/read";
  secondRequest.params = mcp::jsonrpc::JsonValue::object();

  std::future<mcp::jsonrpc::Response> firstFuture = router.sendRequest(context, firstRequest);
  std::future<mcp::jsonrpc::Response> secondFuture = router.sendRequest(context, secondRequest);

  REQUIRE(messageCount(outboundMessages, outboundMessagesMutex) == 2);

  mcp::jsonrpc::SuccessResponse secondResponse;
  secondResponse.id = kOutboundRequestIdB;
  secondResponse.result = mcp::jsonrpc::JsonValue::object();

  mcp::jsonrpc::SuccessResponse firstResponse;
  firstResponse.id = kOutboundRequestIdA;
  firstResponse.result = mcp::jsonrpc::JsonValue::object();

  REQUIRE(router.dispatchResponse(context, mcp::jsonrpc::Response {secondResponse}));
  REQUIRE(router.dispatchResponse(context, mcp::jsonrpc::Response {firstResponse}));

  const mcp::jsonrpc::Response firstResult = firstFuture.get();
  const mcp::jsonrpc::Response secondResult = secondFuture.get();

  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(firstResult));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(secondResult));
  REQUIRE(hasIdValue(std::get<mcp::jsonrpc::SuccessResponse>(firstResult).id, kOutboundRequestIdA));
  REQUIRE(hasIdValue(std::get<mcp::jsonrpc::SuccessResponse>(secondResult).id, kOutboundRequestIdB));
}

TEST_CASE("Router times out outbound requests, emits cancellation, and ignores late responses", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  std::mutex outboundMessagesMutex;
  std::vector<mcp::jsonrpc::Message> outboundMessages;
  router.setOutboundMessageSender([&outboundMessages, &outboundMessagesMutex](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
                                  { recordMessage(outboundMessages, outboundMessagesMutex, std::move(message)); });

  const mcp::jsonrpc::RequestContext context;

  mcp::jsonrpc::Request request;
  request.id = kTimeoutRequestId;
  request.method = "tools/call";
  request.params = mcp::jsonrpc::JsonValue::object();

  mcp::jsonrpc::OutboundRequestOptions options;
  options.timeout = std::chrono::milliseconds(kRequestTimeoutMillis);

  std::future<mcp::jsonrpc::Response> responseFuture = router.sendRequest(context, request, options);
  REQUIRE(responseFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);

  const mcp::jsonrpc::Response timeoutResponse = responseFuture.get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(timeoutResponse));

  const auto &timeoutError = std::get<mcp::jsonrpc::ErrorResponse>(timeoutResponse);
  REQUIRE(timeoutError.id.has_value());
  if (timeoutError.id.has_value())
  {
    REQUIRE(hasIdValue(*timeoutError.id, kTimeoutRequestId));
  }
  REQUIRE(timeoutError.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInternalError));

  waitForMessageCount(outboundMessages, outboundMessagesMutex, 2);
  REQUIRE(messageCount(outboundMessages, outboundMessagesMutex) == 2);

  const std::vector<mcp::jsonrpc::Message> snapshot = copyMessages(outboundMessages, outboundMessagesMutex);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Notification>(snapshot[1]));

  const auto &cancelNotification = std::get<mcp::jsonrpc::Notification>(snapshot[1]);
  REQUIRE(cancelNotification.method == "notifications/cancelled");
  REQUIRE(cancelNotification.params.has_value());
  if (cancelNotification.params.has_value())
  {
    REQUIRE(cancelNotification.params->at("requestId").as<std::int64_t>() == kTimeoutRequestId);
  }

  mcp::jsonrpc::SuccessResponse lateResponse;
  lateResponse.id = kTimeoutRequestId;
  lateResponse.result = mcp::jsonrpc::JsonValue::object();
  REQUIRE_FALSE(router.dispatchResponse(context, mcp::jsonrpc::Response {lateResponse}));
}

TEST_CASE("Router ignores unknown response ids and rejects responses to notifications", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  router.setOutboundMessageSender([](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Message &) -> void {});

  const mcp::jsonrpc::RequestContext context;

  mcp::jsonrpc::Notification notification;
  notification.method = "notifications/ping";
  notification.params = mcp::jsonrpc::JsonValue::object();
  router.sendNotification(context, notification);

  mcp::jsonrpc::SuccessResponse unknownResponse;
  unknownResponse.id = kUnknownResponseId;
  unknownResponse.result = mcp::jsonrpc::JsonValue::object();
  REQUIRE_FALSE(router.dispatchResponse(context, mcp::jsonrpc::Response {unknownResponse}));
}

TEST_CASE("Router does not emit cancellation notification when initialize times out", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  std::mutex outboundMessagesMutex;
  std::vector<mcp::jsonrpc::Message> outboundMessages;
  router.setOutboundMessageSender([&outboundMessages, &outboundMessagesMutex](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
                                  { recordMessage(outboundMessages, outboundMessagesMutex, std::move(message)); });

  const mcp::jsonrpc::RequestContext context;

  mcp::jsonrpc::Request initializeRequest;
  initializeRequest.id = kInitializeRequestId;
  initializeRequest.method = "initialize";
  initializeRequest.params = mcp::jsonrpc::JsonValue::object();

  mcp::jsonrpc::OutboundRequestOptions options;
  options.timeout = std::chrono::milliseconds(kInitializeTimeoutMillis);

  const std::future<mcp::jsonrpc::Response> responseFuture = router.sendRequest(context, initializeRequest, options);
  REQUIRE(responseFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);

  std::this_thread::sleep_for(std::chrono::milliseconds(kInitializePostTimeoutWaitMillis));
  REQUIRE(messageCount(outboundMessages, outboundMessagesMutex) == 1);

  const std::vector<mcp::jsonrpc::Message> snapshot = copyMessages(outboundMessages, outboundMessagesMutex);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(snapshot[0]));
}

TEST_CASE("Router forwards active progress notifications and stops after completion", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  router.setOutboundMessageSender([](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Message &) -> void {});

  std::vector<mcp::jsonrpc::ProgressUpdate> updates;

  const mcp::jsonrpc::RequestContext context;
  mcp::jsonrpc::Request request;
  request.id = kProgressRequestId;
  request.method = "tools/call";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["_meta"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["_meta"]["progressToken"] = "tok-1";

  mcp::jsonrpc::OutboundRequestOptions options;
  options.onProgress = [&updates](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::ProgressUpdate &update) -> void { updates.push_back(update); };

  std::future<mcp::jsonrpc::Response> responseFuture = router.sendRequest(context, request, options);

  mcp::jsonrpc::Notification progressNotification;
  progressNotification.method = "notifications/progress";
  progressNotification.params = mcp::jsonrpc::JsonValue::object();
  (*progressNotification.params)["progressToken"] = "tok-1";
  (*progressNotification.params)["progress"] = kProgressQuarter;
  (*progressNotification.params)["total"] = 1.0;
  (*progressNotification.params)["message"] = "25%";

  router.dispatchNotification(context, progressNotification);
  REQUIRE(updates.size() == 1);
  REQUIRE(std::holds_alternative<std::string>(updates[0].progressToken));
  REQUIRE(std::get<std::string>(updates[0].progressToken) == "tok-1");

  mcp::jsonrpc::SuccessResponse success;
  success.id = kProgressRequestId;
  success.result = mcp::jsonrpc::JsonValue::object();
  REQUIRE(router.dispatchResponse(context, mcp::jsonrpc::Response {success}));

  mcp::jsonrpc::Notification lateProgressNotification = progressNotification;
  (*lateProgressNotification.params)["progress"] = kProgressLate;
  router.dispatchNotification(context, lateProgressNotification);
  REQUIRE(updates.size() == 1);

  mcp::jsonrpc::Notification nonMonotonicProgress = progressNotification;
  (*nonMonotonicProgress.params)["progress"] = 0.1;
  router.dispatchNotification(context, nonMonotonicProgress);
  REQUIRE(updates.size() == 1);

  const mcp::jsonrpc::Response finalResponse = responseFuture.get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(finalResponse));
}

TEST_CASE("Router can emit progress notifications from an inbound request progress token", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  std::mutex outboundMessagesMutex;
  std::vector<mcp::jsonrpc::Message> outboundMessages;
  router.setOutboundMessageSender([&outboundMessages, &outboundMessagesMutex](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
                                  { recordMessage(outboundMessages, outboundMessagesMutex, std::move(message)); });

  std::promise<void> unblockHandler;
  auto unblockFuture = unblockHandler.get_future();
  router.registerRequestHandler("tools/call",
                                [&unblockFuture](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                {
                                  return std::async(std::launch::async,
                                                    [&unblockFuture, request]() -> mcp::jsonrpc::Response
                                                    {
                                                      unblockFuture.wait();
                                                      mcp::jsonrpc::SuccessResponse success;
                                                      success.id = request.id;
                                                      success.result = mcp::jsonrpc::JsonValue::object();
                                                      return mcp::jsonrpc::Response {success};
                                                    });
                                });

  const mcp::jsonrpc::RequestContext context;
  mcp::jsonrpc::Request inboundRequest;
  inboundRequest.id = kInboundProgressRequestId;
  inboundRequest.method = "tools/call";
  inboundRequest.params = mcp::jsonrpc::JsonValue::object();
  (*inboundRequest.params)["_meta"] = mcp::jsonrpc::JsonValue::object();
  (*inboundRequest.params)["_meta"]["progressToken"] = "progress-5150";

  std::future<mcp::jsonrpc::Response> responseFuture = router.dispatchRequest(context, inboundRequest);

  REQUIRE(router.emitProgress(context, inboundRequest, kProgressHalf, 1.0, "halfway"));
  REQUIRE(messageCount(outboundMessages, outboundMessagesMutex) == 1);

  REQUIRE_FALSE(router.emitProgress(context, inboundRequest, 0.4, 1.0, "backwards"));
  REQUIRE(messageCount(outboundMessages, outboundMessagesMutex) == 1);

  unblockHandler.set_value();
  REQUIRE(responseFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);
  static_cast<void>(responseFuture.get());

  REQUIRE_FALSE(router.emitProgress(context, inboundRequest, kProgressLate, 1.0, "late"));

  const std::vector<mcp::jsonrpc::Message> snapshot = copyMessages(outboundMessages, outboundMessagesMutex);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Notification>(snapshot[0]));

  const auto &progressNotification = std::get<mcp::jsonrpc::Notification>(snapshot[0]);
  REQUIRE(progressNotification.method == "notifications/progress");
  REQUIRE(progressNotification.params.has_value());
  if (progressNotification.params.has_value())
  {
    REQUIRE(progressNotification.params->at("progressToken").as<std::string>() == "progress-5150");
    REQUIRE(progressNotification.params->at("progress").as<double>() == Catch::Approx(kProgressHalf));
  }
}

TEST_CASE("Router timeout uses tasks/cancel for task-augmented requests", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  std::mutex outboundMessagesMutex;
  std::vector<mcp::jsonrpc::Message> outboundMessages;
  router.setOutboundMessageSender([&outboundMessages, &outboundMessagesMutex](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
                                  { recordMessage(outboundMessages, outboundMessagesMutex, std::move(message)); });

  const mcp::jsonrpc::RequestContext context;

  mcp::jsonrpc::Request request;
  request.id = kTaskTimeoutRequestId;
  request.method = "tools/call";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["name"] = "long-running";
  (*request.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["task"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["taskId"] = "task-6160";

  mcp::jsonrpc::OutboundRequestOptions options;
  options.timeout = std::chrono::milliseconds(kRequestTimeoutMillis);

  std::future<mcp::jsonrpc::Response> responseFuture = router.sendRequest(context, request, options);
  REQUIRE(responseFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);

  waitForMessageCount(outboundMessages, outboundMessagesMutex, 2);
  REQUIRE(messageCount(outboundMessages, outboundMessagesMutex) == 2);

  const std::vector<mcp::jsonrpc::Message> snapshot = copyMessages(outboundMessages, outboundMessagesMutex);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(snapshot[1]));
  const auto &cancelRequest = std::get<mcp::jsonrpc::Request>(snapshot[1]);
  REQUIRE(cancelRequest.method == "tasks/cancel");
  REQUIRE(cancelRequest.params.has_value());
  if (cancelRequest.params.has_value())
  {
    REQUIRE(cancelRequest.params->at("taskId").as<std::string>() == "task-6160");
  }
}
// NOLINTEND(readability-function-cognitive-complexity)
