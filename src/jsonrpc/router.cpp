#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>

namespace mcp::jsonrpc
{
namespace detail
{

static constexpr const char *kCancelledNotificationMethod = "notifications/cancelled";
static constexpr const char *kProgressNotificationMethod = "notifications/progress";
static constexpr const char *kDefaultSender = "__default_sender__";
static constexpr std::size_t kIntegralRequestIdHashSeed = 0x9e3779b97f4a7c15ULL;
static constexpr std::size_t kStringRequestIdHashSeed = 0x85ebca6bULL;

static auto senderKey(const RequestContext &context) -> std::string
{
  if (context.sessionId.has_value())
  {
    return *context.sessionId;
  }

  return kDefaultSender;
}

static auto requestIdToJson(const RequestId &requestId) -> JsonValue
{
  if (std::holds_alternative<std::int64_t>(requestId))
  {
    return {std::get<std::int64_t>(requestId)};
  }

  return {std::get<std::string>(requestId)};
}

static auto jsonToRequestId(const JsonValue &value) -> std::optional<RequestId>
{
  if (value.is_string())
  {
    return RequestId {value.as<std::string>()};
  }

  if (value.is_int64())
  {
    return RequestId {value.as<std::int64_t>()};
  }

  if (value.is_uint64())
  {
    const std::uint64_t unsignedValue = value.as<std::uint64_t>();
    if (unsignedValue <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
      return RequestId {static_cast<std::int64_t>(unsignedValue)};
    }
  }

  return std::nullopt;
}

static auto jsonToNumber(const JsonValue &value) -> std::optional<double>
{
  if (value.is_double())
  {
    return value.as<double>();
  }

  if (value.is_int64())
  {
    return static_cast<double>(value.as<std::int64_t>());
  }

  if (value.is_uint64())
  {
    return static_cast<double>(value.as<std::uint64_t>());
  }

  return std::nullopt;
}

template<typename T>
static auto makeReadyFuture(T value) -> std::future<T>
{
  std::promise<T> promise;
  promise.set_value(std::move(value));
  return promise.get_future();
}

static auto extractProgressToken(const Request &request) -> std::optional<RequestId>
{
  if (!request.params.has_value())
  {
    return std::nullopt;
  }

  const JsonValue &params = *request.params;
  if (!params.is_object() || !params.contains("_meta"))
  {
    return std::nullopt;
  }

  const JsonValue &meta = params.at("_meta");
  if (!meta.is_object() || !meta.contains("progressToken"))
  {
    return std::nullopt;
  }

  return jsonToRequestId(meta.at("progressToken"));
}

static auto parseProgressNotification(const Notification &notification) -> std::optional<ProgressUpdate>
{
  if (notification.method != kProgressNotificationMethod || !notification.params.has_value())
  {
    return std::nullopt;
  }

  const JsonValue &params = *notification.params;
  if (!params.is_object() || !params.contains("progressToken") || !params.contains("progress"))
  {
    return std::nullopt;
  }

  const std::optional<RequestId> progressToken = jsonToRequestId(params.at("progressToken"));
  const std::optional<double> progress = jsonToNumber(params.at("progress"));
  if (!progressToken.has_value() || !progress.has_value())
  {
    return std::nullopt;
  }

  ProgressUpdate update;
  update.progressToken = *progressToken;
  update.progress = *progress;
  if (params.contains("total"))
  {
    update.total = jsonToNumber(params.at("total"));
  }

  if (params.contains("message") && params.at("message").is_string())
  {
    update.message = params.at("message").as<std::string>();
  }

  update.additionalProperties = params;
  update.additionalProperties.erase("progressToken");
  update.additionalProperties.erase("progress");
  update.additionalProperties.erase("total");
  update.additionalProperties.erase("message");
  return update;
}

static auto responseId(const Response &response) -> std::optional<RequestId>
{
  return std::visit(
    [](const auto &typedResponse) -> std::optional<RequestId>
    {
      using ResponseType = std::decay_t<decltype(typedResponse)>;
      if constexpr (std::is_same_v<ResponseType, SuccessResponse>)
      {
        return typedResponse.id;
      }

      return typedResponse.id;
    },
    response);
}

static auto makeInternalErrorResponse(const RequestId &requestId, std::string message) -> Response
{
  return Response {makeErrorResponse(makeInternalError(std::nullopt, std::move(message)), requestId)};
}

static auto setPromiseValueNoThrow(std::promise<Response> &promise, Response response) noexcept -> void
{
  try
  {
    promise.set_value(std::move(response));
  }
  catch (const std::future_error &error)
  {
    static_cast<void>(error);
    // Promise was already satisfied or has no shared state.
  }
}

}  // namespace detail

auto Router::RequestIdHash::operator()(const RequestId &requestId) const noexcept -> std::size_t
{
  if (std::holds_alternative<std::int64_t>(requestId))
  {
    return std::hash<std::int64_t> {}(std::get<std::int64_t>(requestId)) ^ detail::kIntegralRequestIdHashSeed;
  }

  return std::hash<std::string> {}(std::get<std::string>(requestId)) ^ detail::kStringRequestIdHashSeed;
}

auto Router::RequestIdEqual::operator()(const RequestId &left, const RequestId &right) const noexcept -> bool
{
  return left == right;
}

Router::Router()
  : timeoutPool_(std::make_unique<boost::asio::thread_pool>(1))
{
}

Router::~Router() noexcept
{
  try
  {
    std::vector<std::shared_ptr<InFlightRequestState>> inFlightRequests;
    {
      const std::scoped_lock lock(mutex_);
      inFlightRequests.reserve(inFlightRequests_.size());
      for (const auto &entry : inFlightRequests_)
      {
        inFlightRequests.push_back(entry.second);
      }

      inFlightRequests_.clear();
      requestIdsByProgressToken_.clear();
    }

    for (const auto &requestState : inFlightRequests)
    {
      if (requestState->timeoutTimer)
      {
        requestState->timeoutTimer->cancel();
      }

      detail::setPromiseValueNoThrow(requestState->promise, detail::makeInternalErrorResponse(requestState->request.id, "Router shutdown before response was received."));
    }

    timeoutPool_.reset();
  }
  catch (const std::exception &error)
  {
    static_cast<void>(error);
  }
}

auto Router::setOutboundMessageSender(OutboundMessageSender sender) -> void
{
  const std::scoped_lock lock(mutex_);
  outboundMessageSender_ = std::move(sender);
}

auto Router::registerRequestHandler(std::string method, RequestHandler handler) -> void
{
  const std::scoped_lock lock(mutex_);
  requestHandlers_[std::move(method)] = std::move(handler);
}

auto Router::registerNotificationHandler(std::string method, NotificationHandler handler) -> void
{
  const std::scoped_lock lock(mutex_);
  notificationHandlers_[std::move(method)] = std::move(handler);
}

auto Router::unregisterHandler(const std::string &method) -> bool
{
  const std::scoped_lock lock(mutex_);

  const bool removedRequestHandler = requestHandlers_.erase(method) > 0;
  const bool removedNotificationHandler = notificationHandlers_.erase(method) > 0;

  return removedRequestHandler || removedNotificationHandler;
}

auto Router::dispatchRequest(const RequestContext &context, const Request &request) const -> std::future<Response>
{
  RequestHandler handler;

  {
    const std::scoped_lock lock(mutex_);

    const std::string sender = detail::senderKey(context);
    auto &senderIds = seenRequestIdsBySender_[sender];
    const bool isNewId = senderIds.insert(request.id).second;
    if (!isNewId)
    {
      return detail::makeReadyFuture(Response {makeErrorResponse(makeInvalidRequestError(std::nullopt, "Duplicate request id for sender within session."), request.id)});
    }

    const auto requestHandlerIt = requestHandlers_.find(request.method);
    if (requestHandlerIt == requestHandlers_.end())
    {
      return detail::makeReadyFuture(Response {makeErrorResponse(makeMethodNotFoundError(), request.id)});
    }

    handler = requestHandlerIt->second;
  }

  try
  {
    return handler(context, request);
  }
  catch (...)
  {
    return detail::makeReadyFuture(Response {makeErrorResponse(makeInternalError(std::nullopt, "Request handler threw an exception."), request.id)});
  }
}

auto Router::dispatchNotification(const RequestContext &context, const Notification &notification) const -> void
{
  const std::optional<ProgressUpdate> progressUpdate = detail::parseProgressNotification(notification);

  ProgressCallback progressCallback;
  NotificationHandler methodHandler;
  {
    const std::scoped_lock lock(mutex_);

    if (progressUpdate.has_value())
    {
      const auto requestIdByTokenIt = requestIdsByProgressToken_.find(progressUpdate->progressToken);
      if (requestIdByTokenIt != requestIdsByProgressToken_.end())
      {
        const auto inFlightRequestIt = inFlightRequests_.find(requestIdByTokenIt->second);
        if (inFlightRequestIt != inFlightRequests_.end())
        {
          progressCallback = inFlightRequestIt->second->progressCallback;
        }
      }
    }

    const auto notificationHandlerIt = notificationHandlers_.find(notification.method);
    if (notificationHandlerIt != notificationHandlers_.end())
    {
      methodHandler = notificationHandlerIt->second;
    }
  }

  if (progressCallback && progressUpdate.has_value())
  {
    progressCallback(context, *progressUpdate);
  }

  if (methodHandler)
  {
    methodHandler(context, notification);
  }
}

auto Router::addInFlightRequest(const std::shared_ptr<InFlightRequestState> &inFlightRequest) -> std::optional<Response>
{
  const std::scoped_lock lock(mutex_);

  if (seenOutboundRequestIds_.find(inFlightRequest->request.id) != seenOutboundRequestIds_.end())
  {
    return Response {makeErrorResponse(makeInvalidRequestError(std::nullopt, "Duplicate outbound request id within session."), inFlightRequest->request.id)};
  }

  if (inFlightRequest->progressToken.has_value() && requestIdsByProgressToken_.find(*inFlightRequest->progressToken) != requestIdsByProgressToken_.end())
  {
    return Response {makeErrorResponse(makeInvalidRequestError(std::nullopt, "Duplicate progress token for active outbound request."), inFlightRequest->request.id)};
  }

  seenOutboundRequestIds_.insert(inFlightRequest->request.id);
  inFlightRequests_[inFlightRequest->request.id] = inFlightRequest;
  if (inFlightRequest->progressToken.has_value())
  {
    requestIdsByProgressToken_[*inFlightRequest->progressToken] = inFlightRequest->request.id;
  }

  return std::nullopt;
}

auto Router::popInFlightRequest(const RequestId &requestId, Router::MarkIgnoredResponseId markIgnoredResponseId) -> std::shared_ptr<InFlightRequestState>
{
  const std::scoped_lock lock(mutex_);

  const auto inFlightIt = inFlightRequests_.find(requestId);
  if (inFlightIt == inFlightRequests_.end())
  {
    return nullptr;
  }

  std::shared_ptr<InFlightRequestState> inFlightRequest = inFlightIt->second;
  inFlightRequests_.erase(inFlightIt);

  if (markIgnoredResponseId == MarkIgnoredResponseId::kYes)
  {
    ignoredResponseIds_.insert(requestId);
  }

  if (inFlightRequest->progressToken.has_value())
  {
    requestIdsByProgressToken_.erase(*inFlightRequest->progressToken);
  }

  return inFlightRequest;
}

auto Router::armRequestTimeout(const std::shared_ptr<InFlightRequestState> &inFlightRequest, std::chrono::milliseconds timeout) -> void
{
  if (timeout.count() <= 0 || timeoutPool_ == nullptr)
  {
    return;
  }

  auto timeoutTimer = std::make_shared<boost::asio::steady_timer>(timeoutPool_->get_executor());
  timeoutTimer->expires_after(timeout);

  {
    const std::scoped_lock lock(mutex_);
    const auto inFlightIt = inFlightRequests_.find(inFlightRequest->request.id);
    if (inFlightIt == inFlightRequests_.end())
    {
      return;
    }

    inFlightIt->second->timeoutTimer = timeoutTimer;
  }

  timeoutTimer->async_wait(
    [this, requestId = inFlightRequest->request.id](const auto &errorCode) -> void
    {
      if (errorCode)
      {
        return;
      }

      handleRequestTimeout(requestId);
    });
}

auto Router::handleRequestTimeout(const RequestId &requestId) -> void
{
  const std::shared_ptr<InFlightRequestState> timedOutRequest = popInFlightRequest(requestId, MarkIgnoredResponseId::kYes);
  if (timedOutRequest == nullptr)
  {
    return;
  }

  detail::setPromiseValueNoThrow(timedOutRequest->promise, detail::makeInternalErrorResponse(requestId, "Request timed out."));

  if (!timedOutRequest->cancelOnTimeout || timedOutRequest->request.method == "initialize")
  {
    return;
  }

  Notification cancelNotification;
  cancelNotification.method = detail::kCancelledNotificationMethod;

  JsonValue cancelParams = JsonValue::object();
  cancelParams["requestId"] = detail::requestIdToJson(requestId);
  cancelParams["reason"] = "Request timed out.";
  cancelNotification.params = std::move(cancelParams);

  dispatchOutboundMessage(timedOutRequest->context, Message {std::move(cancelNotification)});
}

auto Router::sendRequest(const RequestContext &context, Request request, OutboundRequestOptions options) -> std::future<Response>
{
  auto inFlightRequest = std::make_shared<InFlightRequestState>();
  inFlightRequest->context = context;
  inFlightRequest->request = std::move(request);
  inFlightRequest->cancelOnTimeout = options.cancelOnTimeout;
  inFlightRequest->progressCallback = std::move(options.onProgress);
  inFlightRequest->progressToken = detail::extractProgressToken(inFlightRequest->request);

  std::future<Response> responseFuture = inFlightRequest->promise.get_future();

  const std::optional<Response> addResult = addInFlightRequest(inFlightRequest);
  if (addResult.has_value())
  {
    detail::setPromiseValueNoThrow(inFlightRequest->promise, *addResult);
    return responseFuture;
  }

  armRequestTimeout(inFlightRequest, options.timeout);

  if (dispatchOutboundMessage(context, Message {inFlightRequest->request}))
  {
    return responseFuture;
  }

  const std::shared_ptr<InFlightRequestState> failedRequest = popInFlightRequest(inFlightRequest->request.id, MarkIgnoredResponseId::kNo);
  if (failedRequest != nullptr)
  {
    if (failedRequest->timeoutTimer)
    {
      failedRequest->timeoutTimer->cancel();
    }

    detail::setPromiseValueNoThrow(failedRequest->promise, detail::makeInternalErrorResponse(failedRequest->request.id, "Failed to send outbound request."));
  }

  return responseFuture;
}

auto Router::sendNotification(const RequestContext &context, Notification notification) const -> void
{
  dispatchOutboundMessage(context, Message {std::move(notification)});
}

auto Router::dispatchResponse(const RequestContext &context, const Response &response) -> bool
{
  static_cast<void>(context);

  const std::optional<RequestId> responseId = detail::responseId(response);
  if (!responseId.has_value())
  {
    return false;
  }

  const std::shared_ptr<InFlightRequestState> inFlightRequest = popInFlightRequest(*responseId, MarkIgnoredResponseId::kNo);
  if (inFlightRequest == nullptr)
  {
    const std::scoped_lock lock(mutex_);
    ignoredResponseIds_.erase(*responseId);
    return false;
  }

  if (inFlightRequest->timeoutTimer)
  {
    inFlightRequest->timeoutTimer->cancel();
  }

  detail::setPromiseValueNoThrow(inFlightRequest->promise, response);
  return true;
}

auto Router::emitProgress(const RequestContext &context, const Request &request, double progress, std::optional<double> total, std::optional<std::string> message) const -> bool
{
  const std::optional<RequestId> progressToken = detail::extractProgressToken(request);
  if (!progressToken.has_value())
  {
    return false;
  }

  return emitProgress(context, *progressToken, progress, total, std::move(message));
}

auto Router::emitProgress(const RequestContext &context, const RequestId &progressToken, double progress, std::optional<double> total, std::optional<std::string> message) const
  -> bool
{
  Notification progressNotification;
  progressNotification.method = detail::kProgressNotificationMethod;

  JsonValue progressParams = JsonValue::object();
  progressParams["progressToken"] = detail::requestIdToJson(progressToken);
  progressParams["progress"] = progress;
  if (total.has_value())
  {
    progressParams["total"] = *total;
  }

  if (message.has_value())
  {
    progressParams["message"] = *message;
  }

  progressNotification.params = std::move(progressParams);
  return dispatchOutboundMessage(context, Message {std::move(progressNotification)});
}

auto Router::dispatchOutboundMessage(const RequestContext &context, Message message) const -> bool
{
  OutboundMessageSender sender;
  {
    const std::scoped_lock lock(mutex_);
    sender = outboundMessageSender_;
  }

  if (!sender)
  {
    return false;
  }

  try
  {
    sender(context, std::move(message));
    return true;
  }
  catch (...)
  {
    return false;
  }
}

}  // namespace mcp::jsonrpc
