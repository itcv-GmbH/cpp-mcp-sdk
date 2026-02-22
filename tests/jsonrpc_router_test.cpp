#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/sdk/errors.hpp>

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
static constexpr std::int64_t kDirectionScopedRequestId = 7171;
static constexpr std::int64_t kOutboundDirectionRequestId = 8181;
static constexpr std::int64_t kProgressRequestIdA = 9101;
static constexpr std::int64_t kProgressRequestIdB = 9102;

static constexpr double kProgressQuarter = 0.25;
static constexpr double kProgressLate = 0.8;
static constexpr double kProgressHalf = 0.5;

static constexpr std::int64_t kRequestTimeoutMillis = 30;
static constexpr std::int64_t kInitializeTimeoutMillis = 20;
static constexpr std::int64_t kResponseWaitMillis = 500;
static constexpr std::int64_t kDestroyWaitMillis = 150;

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

class MessageCapture
{
public:
  auto record(mcp::jsonrpc::Message message) -> void
  {
    {
      const std::scoped_lock lock(mutex_);
      messages_.push_back(std::move(message));
    }

    messagesCv_.notify_all();
  }

  [[nodiscard]] auto waitForMessageCount(std::size_t expectedCount, std::chrono::milliseconds timeout) -> bool
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return messagesCv_.wait_for(lock, timeout, [&]() -> bool { return messages_.size() >= expectedCount; });
  }

  [[nodiscard]] auto messageCount() const -> std::size_t
  {
    const std::scoped_lock lock(mutex_);
    return messages_.size();
  }

  [[nodiscard]] auto copyMessages() const -> std::vector<mcp::jsonrpc::Message>
  {
    const std::scoped_lock lock(mutex_);
    return messages_;
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable messagesCv_;
  std::vector<mcp::jsonrpc::Message> messages_;
};

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
  MessageCapture outboundMessages;
  router.setOutboundMessageSender([&outboundMessages](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
                                  { outboundMessages.record(std::move(message)); });

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

  REQUIRE(outboundMessages.waitForMessageCount(2, std::chrono::milliseconds(kResponseWaitMillis)));
  REQUIRE(outboundMessages.messageCount() == 2);

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
  MessageCapture outboundMessages;
  router.setOutboundMessageSender([&outboundMessages](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
                                  { outboundMessages.record(std::move(message)); });

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

  REQUIRE(outboundMessages.waitForMessageCount(2, std::chrono::milliseconds(kResponseWaitMillis)));
  REQUIRE(outboundMessages.messageCount() == 2);

  const std::vector<mcp::jsonrpc::Message> snapshot = outboundMessages.copyMessages();
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

  mcp::jsonrpc::Request request;
  request.id = kOutboundRequestIdA;
  request.method = "tools/call";
  request.params = mcp::jsonrpc::JsonValue::object();
  std::future<mcp::jsonrpc::Response> responseFuture = router.sendRequest(context, request);

  REQUIRE(responseFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);

  mcp::jsonrpc::Notification notification;
  notification.method = "notifications/ping";
  notification.params = mcp::jsonrpc::JsonValue::object();
  router.sendNotification(context, notification);

  mcp::jsonrpc::SuccessResponse unknownResponse;
  unknownResponse.id = kUnknownResponseId;
  unknownResponse.result = mcp::jsonrpc::JsonValue::object();
  REQUIRE_FALSE(router.dispatchResponse(context, mcp::jsonrpc::Response {unknownResponse}));

  REQUIRE(responseFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);

  mcp::jsonrpc::ErrorResponse responseWithoutId;
  responseWithoutId.error = mcp::jsonrpc::makeInternalError(std::nullopt, "spurious response");
  REQUIRE_FALSE(router.dispatchResponse(context, mcp::jsonrpc::Response {responseWithoutId}));
  REQUIRE(responseFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);

  mcp::jsonrpc::SuccessResponse expectedResponse;
  expectedResponse.id = kOutboundRequestIdA;
  expectedResponse.result = mcp::jsonrpc::JsonValue::object();
  REQUIRE(router.dispatchResponse(context, mcp::jsonrpc::Response {expectedResponse}));

  REQUIRE(responseFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);
  const mcp::jsonrpc::Response finalResponse = responseFuture.get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(finalResponse));
}

TEST_CASE("Router does not emit cancellation notification when initialize times out", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  MessageCapture outboundMessages;
  std::promise<void> cancelPromise;
  std::future<void> cancelFuture = cancelPromise.get_future();
  std::atomic_bool cancelObserved {false};
  router.setOutboundMessageSender(
    [&outboundMessages, &cancelPromise, &cancelObserved](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
    {
      if (std::holds_alternative<mcp::jsonrpc::Notification>(message))
      {
        const auto &notification = std::get<mcp::jsonrpc::Notification>(message);
        if (notification.method == "notifications/cancelled" && !cancelObserved.exchange(true))
        {
          cancelPromise.set_value();
        }
      }

      outboundMessages.record(std::move(message));
    });

  const mcp::jsonrpc::RequestContext context;

  mcp::jsonrpc::Request initializeRequest;
  initializeRequest.id = kInitializeRequestId;
  initializeRequest.method = "initialize";
  initializeRequest.params = mcp::jsonrpc::JsonValue::object();

  mcp::jsonrpc::OutboundRequestOptions options;
  options.timeout = std::chrono::milliseconds(kInitializeTimeoutMillis);

  const std::future<mcp::jsonrpc::Response> responseFuture = router.sendRequest(context, initializeRequest, options);
  REQUIRE(responseFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);

  REQUIRE(cancelFuture.wait_for(std::chrono::milliseconds(kInitializeTimeoutMillis * 3)) == std::future_status::timeout);
  REQUIRE(outboundMessages.waitForMessageCount(1, std::chrono::milliseconds(kResponseWaitMillis)));
  REQUIRE(outboundMessages.messageCount() == 1);

  const std::vector<mcp::jsonrpc::Message> snapshot = outboundMessages.copyMessages();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(snapshot[0]));
}

TEST_CASE("Router ignores cancellation notifications for outbound requests", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  router.setOutboundMessageSender([](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Message &) -> void {});

  mcp::jsonrpc::RequestContext context;
  context.sessionId = "sender-a";

  mcp::jsonrpc::Request outboundRequest;
  outboundRequest.id = kOutboundDirectionRequestId;
  outboundRequest.method = "resources/read";
  outboundRequest.params = mcp::jsonrpc::JsonValue::object();

  std::future<mcp::jsonrpc::Response> outboundFuture = router.sendRequest(context, outboundRequest);
  REQUIRE(outboundFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);

  mcp::jsonrpc::Notification cancellation;
  cancellation.method = "notifications/cancelled";
  cancellation.params = mcp::jsonrpc::JsonValue::object();
  (*cancellation.params)["requestId"] = kOutboundDirectionRequestId;
  router.dispatchNotification(context, cancellation);

  REQUIRE(outboundFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);

  mcp::jsonrpc::SuccessResponse response;
  response.id = kOutboundDirectionRequestId;
  response.result = mcp::jsonrpc::JsonValue::object();
  REQUIRE(router.dispatchResponse(context, mcp::jsonrpc::Response {response}));

  REQUIRE(outboundFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(outboundFuture.get()));
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

TEST_CASE("Router routes progress notifications by token without leaks", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  router.setOutboundMessageSender([](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Message &) -> void {});

  std::vector<mcp::jsonrpc::ProgressUpdate> updatesA;
  std::vector<mcp::jsonrpc::ProgressUpdate> updatesB;

  const mcp::jsonrpc::RequestContext context;

  mcp::jsonrpc::Request requestA;
  requestA.id = kProgressRequestIdA;
  requestA.method = "tools/call";
  requestA.params = mcp::jsonrpc::JsonValue::object();
  (*requestA.params)["_meta"] = mcp::jsonrpc::JsonValue::object();
  (*requestA.params)["_meta"]["progressToken"] = "123";

  mcp::jsonrpc::Request requestB;
  requestB.id = kProgressRequestIdB;
  requestB.method = "resources/read";
  requestB.params = mcp::jsonrpc::JsonValue::object();
  (*requestB.params)["_meta"] = mcp::jsonrpc::JsonValue::object();
  (*requestB.params)["_meta"]["progressToken"] = "tok-b";

  mcp::jsonrpc::OutboundRequestOptions optionsA;
  optionsA.onProgress = [&updatesA](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::ProgressUpdate &update) -> void { updatesA.push_back(update); };

  mcp::jsonrpc::OutboundRequestOptions optionsB;
  optionsB.onProgress = [&updatesB](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::ProgressUpdate &update) -> void { updatesB.push_back(update); };

  std::future<mcp::jsonrpc::Response> futureA = router.sendRequest(context, requestA, optionsA);
  std::future<mcp::jsonrpc::Response> futureB = router.sendRequest(context, requestB, optionsB);

  mcp::jsonrpc::Notification tokenAProgress;
  tokenAProgress.method = "notifications/progress";
  tokenAProgress.params = mcp::jsonrpc::JsonValue::object();
  (*tokenAProgress.params)["progressToken"] = "123";
  (*tokenAProgress.params)["progress"] = kProgressQuarter;
  router.dispatchNotification(context, tokenAProgress);

  REQUIRE(updatesA.size() == 1);
  REQUIRE(updatesB.empty());

  mcp::jsonrpc::Notification tokenBProgress = tokenAProgress;
  (*tokenBProgress.params)["progressToken"] = "tok-b";
  (*tokenBProgress.params)["progress"] = kProgressHalf;
  router.dispatchNotification(context, tokenBProgress);

  REQUIRE(updatesA.size() == 1);
  REQUIRE(updatesB.size() == 1);

  mcp::jsonrpc::Notification wrongTypeTokenProgress = tokenAProgress;
  (*wrongTypeTokenProgress.params)["progressToken"] = 123;
  (*wrongTypeTokenProgress.params)["progress"] = kProgressLate;
  router.dispatchNotification(context, wrongTypeTokenProgress);

  REQUIRE(updatesA.size() == 1);
  REQUIRE(updatesB.size() == 1);

  mcp::jsonrpc::Notification unknownTokenProgress = tokenAProgress;
  (*unknownTokenProgress.params)["progressToken"] = "tok-unknown";
  router.dispatchNotification(context, unknownTokenProgress);

  REQUIRE(updatesA.size() == 1);
  REQUIRE(updatesB.size() == 1);

  mcp::jsonrpc::SuccessResponse responseA;
  responseA.id = kProgressRequestIdA;
  responseA.result = mcp::jsonrpc::JsonValue::object();
  REQUIRE(router.dispatchResponse(context, mcp::jsonrpc::Response {responseA}));

  mcp::jsonrpc::Notification lateTokenAProgress = tokenAProgress;
  (*lateTokenAProgress.params)["progress"] = 0.9;
  router.dispatchNotification(context, lateTokenAProgress);

  REQUIRE(updatesA.size() == 1);
  REQUIRE(updatesB.size() == 1);

  mcp::jsonrpc::Notification laterTokenBProgress = tokenBProgress;
  (*laterTokenBProgress.params)["progress"] = kProgressLate;
  router.dispatchNotification(context, laterTokenBProgress);

  REQUIRE(updatesA.size() == 1);
  REQUIRE(updatesB.size() == 2);

  mcp::jsonrpc::SuccessResponse responseB;
  responseB.id = kProgressRequestIdB;
  responseB.result = mcp::jsonrpc::JsonValue::object();
  REQUIRE(router.dispatchResponse(context, mcp::jsonrpc::Response {responseB}));

  mcp::jsonrpc::Notification lateTokenBProgress = tokenBProgress;
  (*lateTokenBProgress.params)["progress"] = 1.0;
  router.dispatchNotification(context, lateTokenBProgress);

  REQUIRE(updatesA.size() == 1);
  REQUIRE(updatesB.size() == 2);

  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(futureA.get()));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(futureB.get()));
}

TEST_CASE("Router can emit progress notifications from an inbound request progress token", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  MessageCapture outboundMessages;
  router.setOutboundMessageSender([&outboundMessages](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
                                  { outboundMessages.record(std::move(message)); });

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
  REQUIRE(outboundMessages.waitForMessageCount(1, std::chrono::milliseconds(kResponseWaitMillis)));
  REQUIRE(outboundMessages.messageCount() == 1);

  REQUIRE_FALSE(router.emitProgress(context, inboundRequest, 0.4, 1.0, "backwards"));
  REQUIRE(outboundMessages.messageCount() == 1);

  unblockHandler.set_value();
  REQUIRE(responseFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);
  static_cast<void>(responseFuture.get());

  REQUIRE_FALSE(router.emitProgress(context, inboundRequest, kProgressLate, 1.0, "late"));

  const std::vector<mcp::jsonrpc::Message> snapshot = outboundMessages.copyMessages();
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

TEST_CASE("Router destruction completes promptly and resolves blocked inbound requests", "[jsonrpc][router]")
{
  auto router = std::make_unique<mcp::jsonrpc::Router>();

  auto blockedResponsePromise = std::make_shared<std::promise<mcp::jsonrpc::Response>>();
  std::promise<void> handlerStartedPromise;
  std::future<void> handlerStartedFuture = handlerStartedPromise.get_future();
  std::atomic_bool handlerStarted {false};

  router->registerRequestHandler(
    "tools/call",
    [blockedResponsePromise, &handlerStartedPromise, &handlerStarted](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &) -> std::future<mcp::jsonrpc::Response>
    {
      if (!handlerStarted.exchange(true))
      {
        handlerStartedPromise.set_value();
      }

      return blockedResponsePromise->get_future();
    });

  const mcp::jsonrpc::RequestContext context;
  mcp::jsonrpc::Request request;
  request.id = std::int64_t {9090};
  request.method = "tools/call";
  request.params = mcp::jsonrpc::JsonValue::object();

  std::future<mcp::jsonrpc::Response> responseFuture = router->dispatchRequest(context, request);
  REQUIRE(handlerStartedFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);

  std::future<void> destroyFuture = std::async(std::launch::async, [router = std::move(router)]() mutable -> void { router.reset(); });

  REQUIRE(destroyFuture.wait_for(std::chrono::milliseconds(kDestroyWaitMillis)) == std::future_status::ready);
  destroyFuture.get();

  const std::future_status responseStatus = responseFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis));

  mcp::jsonrpc::SuccessResponse handlerResponse;
  handlerResponse.id = request.id;
  handlerResponse.result = mcp::jsonrpc::JsonValue::object();
  blockedResponsePromise->set_value(mcp::jsonrpc::Response {handlerResponse});

  REQUIRE(responseStatus == std::future_status::ready);

  if (responseStatus == std::future_status::ready)
  {
    const mcp::jsonrpc::Response completionResponse = responseFuture.get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(completionResponse));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(completionResponse);
    REQUIRE(error.id.has_value());
    if (error.id.has_value())
    {
      REQUIRE(hasIdValue(*error.id, 9090));
    }
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInternalError));
  }
}

TEST_CASE("Router timeout sends protocol-valid tasks/cancel after late CreateTaskResult", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  MessageCapture outboundMessages;
  router.setOutboundMessageSender([&outboundMessages](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
                                  { outboundMessages.record(std::move(message)); });

  const mcp::jsonrpc::RequestContext context;

  mcp::jsonrpc::Request request;
  request.id = kTaskTimeoutRequestId;
  request.method = "tools/call";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["name"] = "long-running";
  (*request.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["task"] = mcp::jsonrpc::JsonValue::object();

  mcp::jsonrpc::OutboundRequestOptions options;
  options.timeout = std::chrono::milliseconds(kRequestTimeoutMillis);

  std::future<mcp::jsonrpc::Response> responseFuture = router.sendRequest(context, request, options);
  REQUIRE(responseFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);

  REQUIRE(outboundMessages.waitForMessageCount(1, std::chrono::milliseconds(kResponseWaitMillis)));
  REQUIRE(outboundMessages.messageCount() == 1);

  mcp::jsonrpc::SuccessResponse lateTaskCreateResponse;
  lateTaskCreateResponse.id = kTaskTimeoutRequestId;
  lateTaskCreateResponse.result = mcp::jsonrpc::JsonValue::object();
  lateTaskCreateResponse.result["task"] = mcp::jsonrpc::JsonValue::object();
  lateTaskCreateResponse.result["task"]["taskId"] = "task-6160";
  lateTaskCreateResponse.result["task"]["status"] = "working";
  lateTaskCreateResponse.result["task"]["createdAt"] = "2026-01-01T00:00:00Z";
  lateTaskCreateResponse.result["task"]["lastUpdatedAt"] = "2026-01-01T00:00:00Z";

  REQUIRE_FALSE(router.dispatchResponse(context, mcp::jsonrpc::Response {lateTaskCreateResponse}));

  REQUIRE(outboundMessages.waitForMessageCount(2, std::chrono::milliseconds(kResponseWaitMillis)));
  REQUIRE(outboundMessages.messageCount() == 2);

  const std::vector<mcp::jsonrpc::Message> snapshot = outboundMessages.copyMessages();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(snapshot[1]));
  const auto &cancelRequest = std::get<mcp::jsonrpc::Request>(snapshot[1]);
  REQUIRE(cancelRequest.method == "tasks/cancel");
  REQUIRE(cancelRequest.params.has_value());
  if (cancelRequest.params.has_value())
  {
    REQUIRE(cancelRequest.params->at("taskId").as<std::string>() == "task-6160");
  }
}

TEST_CASE("Router scopes inbound cancellation by sender and direction", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  MessageCapture outboundMessages;
  router.setOutboundMessageSender([&outboundMessages](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
                                  { outboundMessages.record(std::move(message)); });

  std::promise<void> releaseInboundHandler;
  auto releaseInboundHandlerFuture = releaseInboundHandler.get_future();
  router.registerRequestHandler("tools/call",
                                [&releaseInboundHandlerFuture](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                {
                                  return std::async(std::launch::async,
                                                    [&releaseInboundHandlerFuture, request]() -> mcp::jsonrpc::Response
                                                    {
                                                      releaseInboundHandlerFuture.wait();
                                                      mcp::jsonrpc::SuccessResponse success;
                                                      success.id = request.id;
                                                      success.result = mcp::jsonrpc::JsonValue::object();
                                                      return mcp::jsonrpc::Response {success};
                                                    });
                                });

  mcp::jsonrpc::RequestContext senderA;
  senderA.sessionId = "sender-a";

  mcp::jsonrpc::RequestContext senderB;
  senderB.sessionId = "sender-b";

  mcp::jsonrpc::Request inboundRequest;
  inboundRequest.id = kDirectionScopedRequestId;
  inboundRequest.method = "tools/call";
  inboundRequest.params = mcp::jsonrpc::JsonValue::object();
  (*inboundRequest.params)["_meta"] = mcp::jsonrpc::JsonValue::object();
  (*inboundRequest.params)["_meta"]["progressToken"] = "dir-token";

  std::future<mcp::jsonrpc::Response> inboundFuture = router.dispatchRequest(senderA, inboundRequest);

  mcp::jsonrpc::Notification wrongSenderCancellation;
  wrongSenderCancellation.method = "notifications/cancelled";
  wrongSenderCancellation.params = mcp::jsonrpc::JsonValue::object();
  (*wrongSenderCancellation.params)["requestId"] = kDirectionScopedRequestId;
  router.dispatchNotification(senderB, wrongSenderCancellation);

  REQUIRE(router.emitProgress(senderA, inboundRequest, kProgressQuarter, 1.0, "still active"));

  mcp::jsonrpc::Notification matchingSenderCancellation = wrongSenderCancellation;
  router.dispatchNotification(senderA, matchingSenderCancellation);

  REQUIRE_FALSE(router.emitProgress(senderA, inboundRequest, kProgressHalf, 1.0, "cancelled"));

  mcp::jsonrpc::Request outboundRequest;
  outboundRequest.id = kDirectionScopedRequestId;
  outboundRequest.method = "resources/read";
  outboundRequest.params = mcp::jsonrpc::JsonValue::object();

  mcp::jsonrpc::OutboundRequestOptions options;
  options.timeout = std::chrono::milliseconds(kRequestTimeoutMillis);
  std::future<mcp::jsonrpc::Response> outboundFuture = router.sendRequest(senderA, outboundRequest, options);
  REQUIRE(outboundFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);

  REQUIRE(outboundMessages.waitForMessageCount(3, std::chrono::milliseconds(kResponseWaitMillis)));
  const std::vector<mcp::jsonrpc::Message> snapshot = outboundMessages.copyMessages();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Notification>(snapshot[2]));
  const auto &outboundCancellation = std::get<mcp::jsonrpc::Notification>(snapshot[2]);
  REQUIRE(outboundCancellation.method == "notifications/cancelled");
  REQUIRE(outboundCancellation.params.has_value());
  if (outboundCancellation.params.has_value())
  {
    REQUIRE(outboundCancellation.params->at("requestId").as<std::int64_t>() == kDirectionScopedRequestId);
  }

  releaseInboundHandler.set_value();
  REQUIRE(inboundFuture.wait_for(std::chrono::milliseconds(kResponseWaitMillis)) == std::future_status::ready);
  static_cast<void>(inboundFuture.get());
}
// NOLINTEND(readability-function-cognitive-complexity)
