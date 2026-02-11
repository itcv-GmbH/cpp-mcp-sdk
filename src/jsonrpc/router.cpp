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
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/util/cancellation.hpp>
#include <mcp/util/progress.hpp>

namespace mcp::jsonrpc
{
namespace detail
{

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

static auto requestIdToString(const RequestId &requestId) -> std::string
{
  if (std::holds_alternative<std::int64_t>(requestId))
  {
    return std::to_string(std::get<std::int64_t>(requestId));
  }

  return std::get<std::string>(requestId);
}

template<typename T>
static auto makeReadyFuture(T value) -> std::future<T>
{
  std::promise<T> promise;
  promise.set_value(std::move(value));
  return promise.get_future();
}

static auto parseProgressNotification(const Notification &notification) -> std::optional<ProgressUpdate>
{
  const std::optional<util::progress::ProgressNotification> parsed = util::progress::parseProgressNotification(notification);
  if (!parsed.has_value())
  {
    return std::nullopt;
  }

  ProgressUpdate update;
  update.progressToken = parsed->progressToken;
  update.progress = parsed->progress;
  update.total = parsed->total;
  update.message = parsed->message;
  update.additionalProperties = parsed->additionalProperties;
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
    waitForInboundWorkers();

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
      pendingTaskCancellationByRequestId_.clear();
      inboundRequestsBySender_.clear();
      inboundRequestIdsByProgressTokenBySender_.clear();
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

auto Router::dispatchRequest(const RequestContext &context, const Request &request) -> std::future<Response>
{
  RequestHandler handler;
  const std::string sender = detail::senderKey(context);

  {
    const std::scoped_lock lock(mutex_);

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

  if (!markInboundRequestActive(context, request))
  {
    return detail::makeReadyFuture(Response {makeErrorResponse(makeInvalidRequestError(std::nullopt, "Duplicate progress token for active inbound request."), request.id)});
  }

  auto responsePromise = std::make_shared<std::promise<Response>>();
  std::future<Response> responseFuture = responsePromise->get_future();

  if (!markInboundWorkerStarted())
  {
    completeInboundRequest(context, request.id);
    return detail::makeReadyFuture(Response {makeErrorResponse(makeInternalError(std::nullopt, "Router is shutting down."), request.id)});
  }

  try
  {
    std::future<Response> handlerFuture = handler(context, request);
    std::thread(
      [this, context, requestId = request.id, responsePromise, handlerFuture = std::move(handlerFuture)]() mutable -> void
      {
        try
        {
          Response response = handlerFuture.get();
          completeInboundRequest(context, requestId);
          detail::setPromiseValueNoThrow(*responsePromise, std::move(response));
        }
        catch (...)
        {
          completeInboundRequest(context, requestId);
          detail::setPromiseValueNoThrow(*responsePromise, Response {makeErrorResponse(makeInternalError(std::nullopt, "Request handler threw an exception."), requestId)});
        }

        markInboundWorkerFinished();
      })
      .detach();

    return responseFuture;
  }
  catch (...)
  {
    markInboundWorkerFinished();
    completeInboundRequest(context, request.id);
    return detail::makeReadyFuture(Response {makeErrorResponse(makeInternalError(std::nullopt, "Request handler threw an exception."), request.id)});
  }
}

auto Router::dispatchNotification(const RequestContext &context, const Notification &notification) -> void
{
  const std::optional<ProgressUpdate> progressUpdate = detail::parseProgressNotification(notification);
  const std::optional<util::cancellation::CancelledNotification> cancelledNotification = util::cancellation::parseCancelledNotification(notification);

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
          std::shared_ptr<InFlightRequestState> requestState = inFlightRequestIt->second;
          const bool monotonic = !requestState->lastObservedProgress.has_value() || progressUpdate->progress > *requestState->lastObservedProgress;
          if (monotonic)
          {
            requestState->lastObservedProgress = progressUpdate->progress;
            progressCallback = requestState->progressCallback;
          }
        }
      }
    }

    if (cancelledNotification.has_value())
    {
      const std::string sender = detail::senderKey(context);
      const auto senderRequestsIt = inboundRequestsBySender_.find(sender);
      if (senderRequestsIt != inboundRequestsBySender_.end())
      {
        const auto inboundRequestIt = senderRequestsIt->second.find(cancelledNotification->requestId);
        if (inboundRequestIt != senderRequestsIt->second.end() && inboundRequestIt->second.method != "initialize" && !inboundRequestIt->second.taskAugmented)
        {
          inboundRequestIt->second.cancelled = true;
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

  if (timedOutRequest->taskAugmented)
  {
    if (timedOutRequest->taskId.has_value())
    {
      Request cancelRequest = util::cancellation::makeTasksCancelRequest(RequestId {std::string("cancel:") + detail::requestIdToString(requestId)}, *timedOutRequest->taskId);
      static_cast<void>(dispatchOutboundMessage(timedOutRequest->context, Message {std::move(cancelRequest)}));
    }
    else
    {
      const std::scoped_lock lock(mutex_);
      pendingTaskCancellationByRequestId_[requestId] = timedOutRequest->context;
    }

    return;
  }

  Notification cancelNotification = util::cancellation::makeCancelledNotification(requestId, std::string("Request timed out."));
  static_cast<void>(dispatchOutboundMessage(timedOutRequest->context, Message {std::move(cancelNotification)}));
}

auto Router::sendRequest(const RequestContext &context, Request request, OutboundRequestOptions options) -> std::future<Response>
{
  auto inFlightRequest = std::make_shared<InFlightRequestState>();
  inFlightRequest->context = context;
  inFlightRequest->request = std::move(request);
  inFlightRequest->cancelOnTimeout = options.cancelOnTimeout;
  inFlightRequest->progressCallback = std::move(options.onProgress);
  inFlightRequest->progressToken = util::progress::extractProgressToken(inFlightRequest->request);
  inFlightRequest->taskAugmented = util::cancellation::isTaskAugmentedRequest(inFlightRequest->request);

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
    std::optional<RequestContext> pendingTaskCancellationContext;
    {
      const std::scoped_lock lock(mutex_);
      if (ignoredResponseIds_.erase(*responseId) > 0)
      {
        const auto pendingTaskCancellationIt = pendingTaskCancellationByRequestId_.find(*responseId);
        if (pendingTaskCancellationIt != pendingTaskCancellationByRequestId_.end())
        {
          pendingTaskCancellationContext = pendingTaskCancellationIt->second;
          pendingTaskCancellationByRequestId_.erase(pendingTaskCancellationIt);
        }
      }
    }

    if (pendingTaskCancellationContext.has_value())
    {
      const std::optional<std::string> taskId = util::cancellation::extractCreateTaskResultTaskId(response);
      if (taskId.has_value())
      {
        Request cancelRequest = util::cancellation::makeTasksCancelRequest(RequestId {std::string("cancel:") + detail::requestIdToString(*responseId)}, *taskId);
        static_cast<void>(dispatchOutboundMessage(*pendingTaskCancellationContext, Message {std::move(cancelRequest)}));
      }
    }

    return false;
  }

  if (inFlightRequest->timeoutTimer)
  {
    inFlightRequest->timeoutTimer->cancel();
  }

  detail::setPromiseValueNoThrow(inFlightRequest->promise, response);
  return true;
}

auto Router::emitProgress(const RequestContext &context, const Request &request, double progress, std::optional<double> total, std::optional<std::string> message) -> bool
{
  const std::optional<RequestId> progressToken = util::progress::extractProgressToken(request);
  if (!progressToken.has_value())
  {
    return false;
  }

  return emitProgress(context, *progressToken, progress, total, std::move(message));
}

auto Router::emitProgress(const RequestContext &context, const RequestId &progressToken, double progress, std::optional<double> total, std::optional<std::string> message) -> bool
{
  {
    const std::scoped_lock lock(mutex_);
    const std::string sender = detail::senderKey(context);
    const auto senderProgressIt = inboundRequestIdsByProgressTokenBySender_.find(sender);
    if (senderProgressIt == inboundRequestIdsByProgressTokenBySender_.end())
    {
      return false;
    }

    const auto requestByTokenIt = senderProgressIt->second.find(progressToken);
    if (requestByTokenIt == senderProgressIt->second.end())
    {
      return false;
    }

    const auto senderRequestsIt = inboundRequestsBySender_.find(sender);
    if (senderRequestsIt == inboundRequestsBySender_.end())
    {
      return false;
    }

    const auto requestIt = senderRequestsIt->second.find(requestByTokenIt->second);
    if (requestIt == senderRequestsIt->second.end())
    {
      return false;
    }

    if (requestIt->second.cancelled)
    {
      return false;
    }

    if (requestIt->second.lastEmittedProgress.has_value() && progress <= *requestIt->second.lastEmittedProgress)
    {
      return false;
    }

    requestIt->second.lastEmittedProgress = progress;
  }

  Notification progressNotification = util::progress::makeProgressNotification(progressToken, progress, total, std::move(message));
  return dispatchOutboundMessage(context, Message {std::move(progressNotification)});
}

auto Router::markInboundRequestActive(const RequestContext &context, const Request &request) -> bool
{
  const std::scoped_lock lock(mutex_);
  const std::string sender = detail::senderKey(context);

  InboundRequestState requestState;
  requestState.method = request.method;
  requestState.taskAugmented = util::cancellation::isTaskAugmentedRequest(request);
  requestState.progressToken = util::progress::extractProgressToken(request);

  auto &senderRequests = inboundRequestsBySender_[sender];
  senderRequests[request.id] = requestState;

  if (requestState.progressToken.has_value())
  {
    auto &requestIdsByToken = inboundRequestIdsByProgressTokenBySender_[sender];
    if (requestIdsByToken.find(*requestState.progressToken) != requestIdsByToken.end())
    {
      senderRequests.erase(request.id);
      return false;
    }

    requestIdsByToken[*requestState.progressToken] = request.id;
  }

  return true;
}

auto Router::markInboundWorkerStarted() -> bool
{
  const std::scoped_lock lock(mutex_);
  if (shuttingDown_)
  {
    return false;
  }

  ++activeInboundWorkers_;
  return true;
}

auto Router::markInboundWorkerFinished() -> void
{
  const std::scoped_lock lock(mutex_);
  if (activeInboundWorkers_ == 0)
  {
    return;
  }

  --activeInboundWorkers_;
  if (activeInboundWorkers_ == 0)
  {
    inboundWorkersDone_.notify_all();
  }
}

auto Router::waitForInboundWorkers() -> void
{
  std::unique_lock<std::mutex> lock(mutex_);
  shuttingDown_ = true;
  inboundWorkersDone_.wait(lock, [this]() -> bool { return activeInboundWorkers_ == 0; });
}

auto Router::completeInboundRequest(const RequestContext &context, const RequestId &requestId) -> void
{
  const std::scoped_lock lock(mutex_);
  const std::string sender = detail::senderKey(context);

  const auto senderRequestsIt = inboundRequestsBySender_.find(sender);
  if (senderRequestsIt == inboundRequestsBySender_.end())
  {
    return;
  }

  const auto requestIt = senderRequestsIt->second.find(requestId);
  if (requestIt == senderRequestsIt->second.end())
  {
    return;
  }

  if (requestIt->second.progressToken.has_value())
  {
    const auto senderTokensIt = inboundRequestIdsByProgressTokenBySender_.find(sender);
    if (senderTokensIt != inboundRequestIdsByProgressTokenBySender_.end())
    {
      senderTokensIt->second.erase(*requestIt->second.progressToken);
      if (senderTokensIt->second.empty())
      {
        inboundRequestIdsByProgressTokenBySender_.erase(senderTokensIt);
      }
    }
  }

  senderRequestsIt->second.erase(requestIt);
  if (senderRequestsIt->second.empty())
  {
    inboundRequestsBySender_.erase(senderRequestsIt);
  }
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
