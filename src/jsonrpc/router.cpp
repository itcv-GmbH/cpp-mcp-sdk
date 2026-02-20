#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <boost/asio/post.hpp>
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
static constexpr std::size_t kInboundCompletionWorkerCount = 4;

// Dedicated thread for safely destroying thread pools.
// This prevents pool destructor from running on pool worker threads (which crashes Boost.Asio).
static std::shared_ptr<std::thread> &getDeleterThread()
{
  static std::shared_ptr<std::thread> deleterThread = []
  {
    auto thread = std::make_shared<std::thread>(
      []
      {
        std::function<void()> task;
        while (true)
        {
          {
            // Sleep in a way that allows shutdown detection
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          }
        }
      });
    thread->detach();
    return thread;
  }();
  return deleterThread;
}

// Queue for deletion tasks - thread-safe
static std::queue<std::function<void()>> &getDeletionQueue()
{
  static std::mutex queueMutex;
  static std::queue<std::function<void()>> queue;
  return queue;
}

// Post a deletion task to the dedicated deleter thread
static void postDeletion(std::function<void()> &&task)
{
  static std::mutex queueMutex;
  static std::condition_variable cv;
  static std::queue<std::function<void()>> queue;
  static std::atomic<bool> running {true};

  static std::thread worker(
    []
    {
      while (running.load(std::memory_order_relaxed))
      {
        std::function<void()> task;
        {
          std::unique_lock lock(queueMutex);
          cv.wait_for(lock, std::chrono::milliseconds(100), [] { return !queue.empty(); });
          if (!queue.empty())
          {
            task = std::move(queue.front());
            queue.pop();
          }
        }
        if (task)
        {
          task();
        }
      }
    });
  static std::once_flag once;
  std::call_once(once, [] { worker.detach(); });

  {
    std::unique_lock lock(queueMutex);
    queue.push(std::move(task));
  }
  cv.notify_one();
}

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

Router::Router(RouterOptions options)
  : inboundState_(std::make_shared<Router::InboundState>())
  , options_(options)
  , timeoutPool_(std::make_unique<boost::asio::thread_pool>(1))
{
  // Create completion pool with custom deleter that ensures destruction
  // runs on a non-pool thread (the deleter thread) to avoid crashes when
  // pool destructor runs on its own worker thread.
  auto rawPool = new boost::asio::thread_pool(detail::kInboundCompletionWorkerCount);
  inboundState_->completionPool = std::shared_ptr<boost::asio::thread_pool>(rawPool,
                                                                            [](boost::asio::thread_pool *pool)
                                                                            {
                                                                              // Post deletion to dedicated thread to ensure pool destructor
                                                                              // never runs on a worker thread
                                                                              detail::postDeletion([pool]() { delete pool; });
                                                                            });
}

Router::~Router() noexcept
{
  try
  {
    std::vector<std::pair<RequestId, std::shared_ptr<std::promise<Response>>>> pendingInboundPromises;

    {
      const std::scoped_lock lock(inboundState_->mutex);
      inboundState_->shuttingDown = true;

      for (auto &entry : inboundState_->inboundResponsePromisesBySender)
      {
        for (auto &promiseEntry : entry.second)
        {
          pendingInboundPromises.emplace_back(promiseEntry.first, std::move(promiseEntry.second));
        }
      }

      inboundState_->inboundResponsePromisesBySender.clear();
      inboundState_->inboundRequestsBySender.clear();
      inboundState_->inboundRequestIdsByProgressTokenBySender.clear();
    }

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
    }

    for (const auto &requestState : inFlightRequests)
    {
      if (requestState->timeoutTimer)
      {
        requestState->timeoutTimer->cancel();
        requestState->timeoutTimer.reset();
      }

      detail::setPromiseValueNoThrow(requestState->promise, detail::makeInternalErrorResponse(requestState->request.id, "Router shutdown before response was received."));
    }

    inFlightRequests.clear();

    for (auto &promiseEntry : pendingInboundPromises)
    {
      if (!promiseEntry.second)
      {
        continue;
      }

      detail::setPromiseValueNoThrow(*promiseEntry.second, detail::makeInternalErrorResponse(promiseEntry.first, "Router shutdown before request handler completed."));
    }

    // Clean up completion pool.
    // The key is to ensure the thread_pool destructor never runs on a worker thread.
    // With our custom deleter, the actual delete is posted to a dedicated thread.
    std::shared_ptr<boost::asio::thread_pool> completionPool;
    {
      const std::scoped_lock lock(inboundState_->mutex);
      completionPool = std::move(inboundState_->completionPool);
    }

    // Handle timeoutPool - stop it but don't wait/join (unsafe with references)
    // The unique_ptr will destroy the pool when it goes out of scope
    if (timeoutPool_)
    {
      timeoutPool_->stop();
    }
    timeoutPool_.reset();

    // Stop the completion pool before releasing - this ensures no more work runs
    if (completionPool)
    {
      completionPool->stop();
    }

    // Release completionPool - custom deleter posts deletion to dedicated thread
    completionPool.reset();
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
  auto responsePromise = std::make_shared<std::promise<Response>>();
  std::future<Response> responseFuture = responsePromise->get_future();

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

  const InboundRequestActivationResult inboundActivation = markInboundRequestActive(context, request, responsePromise);
  if (inboundActivation != InboundRequestActivationResult::kAccepted)
  {
    if (inboundActivation == InboundRequestActivationResult::kShuttingDown)
    {
      return detail::makeReadyFuture(Response {makeErrorResponse(makeInternalError(std::nullopt, "Router is shutting down."), request.id)});
    }

    if (inboundActivation == InboundRequestActivationResult::kDuplicateProgressToken)
    {
      return detail::makeReadyFuture(Response {makeErrorResponse(makeInvalidRequestError(std::nullopt, "Duplicate progress token for active inbound request."), request.id)});
    }

    return detail::makeReadyFuture(
      Response {makeErrorResponse(makeInvalidRequestError(std::nullopt, "Exceeded configured max concurrent in-flight inbound requests."), request.id)});
  }

  try
  {
    std::future<Response> handlerFuture = handler(context, request);

    // Check if we have a completion pool
    std::shared_ptr<boost::asio::thread_pool> completionPool;
    {
      const std::scoped_lock lock(inboundState_->mutex);
      completionPool = inboundState_->completionPool;
    }

    if (!completionPool)
    {
      completeInboundRequest(context, request.id);
      detail::setPromiseValueNoThrow(*responsePromise, Response {makeErrorResponse(makeInternalError(std::nullopt, "Router is shutting down."), request.id)});
      return responseFuture;
    }

    const std::shared_ptr<Router::InboundState> inboundState = inboundState_;

    // Capture shared_ptr to keep the completion pool alive while handler may be running.
    // The custom deleter ensures that when the last shared_ptr is released, deletion
    // happens on a dedicated thread (not a pool worker thread).
    std::shared_ptr<boost::asio::thread_pool> poolKeepalive = completionPool;
    boost::asio::post(*completionPool,
                      [poolKeepalive, inboundState, context, requestId = request.id, responsePromise, handlerFuture = std::move(handlerFuture)]() mutable noexcept
                      {
                        // Wrap entire handler in try/catch to prevent any exceptions from escaping
                        // (addresses clang-tidy bugprone-exception-escape)
                        try
                        {
                          try
                          {
                            Response response = handlerFuture.get();
                            Router::completeInboundRequest(inboundState, context, requestId);
                            detail::setPromiseValueNoThrow(*responsePromise, std::move(response));
                          }
                          catch (...)
                          {
                            Router::completeInboundRequest(inboundState, context, requestId);
                            detail::setPromiseValueNoThrow(*responsePromise,
                                                           Response {makeErrorResponse(makeInternalError(std::nullopt, "Request handler threw an exception."), requestId)});
                          }
                        }
                        catch (...)
                        {
                          // Suppress any exceptions - must not escape the handler
                        }
                      });

    return responseFuture;
  }
  catch (...)
  {
    completeInboundRequest(context, request.id);
    detail::setPromiseValueNoThrow(*responsePromise, Response {makeErrorResponse(makeInternalError(std::nullopt, "Request handler threw an exception."), request.id)});
    return responseFuture;
  }
}

auto Router::dispatchNotification(const RequestContext &context, const Notification &notification) -> void
{
  const std::optional<ProgressUpdate> progressUpdate = detail::parseProgressNotification(notification);
  const std::optional<util::cancellation::CancelledNotification> cancelledNotification = util::cancellation::parseCancelledNotification(notification);

  ProgressCallback progressCallback;
  if (progressUpdate.has_value())
  {
    const std::scoped_lock lock(mutex_);

    const auto requestIdByTokenIt = requestIdsByProgressToken_.find(progressUpdate->progressToken);
    if (requestIdByTokenIt != requestIdsByProgressToken_.end())
    {
      const auto inFlightRequestIt = inFlightRequests_.find(requestIdByTokenIt->second);
      if (inFlightRequestIt != inFlightRequests_.end())
      {
        const std::shared_ptr<InFlightRequestState> requestState = inFlightRequestIt->second;
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
    const std::scoped_lock lock(inboundState_->mutex);
    const std::string sender = detail::senderKey(context);
    const auto senderRequestsIt = inboundState_->inboundRequestsBySender.find(sender);
    if (senderRequestsIt != inboundState_->inboundRequestsBySender.end())
    {
      const auto inboundRequestIt = senderRequestsIt->second.find(cancelledNotification->requestId);
      if (inboundRequestIt != senderRequestsIt->second.end() && inboundRequestIt->second.method != "initialize" && !inboundRequestIt->second.taskAugmented)
      {
        inboundRequestIt->second.cancelled = true;
      }
    }
  }

  NotificationHandler methodHandler;
  {
    const std::scoped_lock lock(mutex_);
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

  if (inFlightRequests_.size() >= options_.maxConcurrentInFlightRequests)
  {
    return Response {makeErrorResponse(makeInvalidRequestError(std::nullopt, "Exceeded configured max concurrent in-flight outbound requests."), inFlightRequest->request.id)};
  }

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

  timedOutRequest->timeoutTimer.reset();

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
      failedRequest->timeoutTimer.reset();
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
    inFlightRequest->timeoutTimer.reset();
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
    const std::scoped_lock lock(inboundState_->mutex);
    const std::string sender = detail::senderKey(context);
    const auto senderProgressIt = inboundState_->inboundRequestIdsByProgressTokenBySender.find(sender);
    if (senderProgressIt == inboundState_->inboundRequestIdsByProgressTokenBySender.end())
    {
      return false;
    }

    const auto requestByTokenIt = senderProgressIt->second.find(progressToken);
    if (requestByTokenIt == senderProgressIt->second.end())
    {
      return false;
    }

    const auto senderRequestsIt = inboundState_->inboundRequestsBySender.find(sender);
    if (senderRequestsIt == inboundState_->inboundRequestsBySender.end())
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

auto Router::markInboundRequestActive(const RequestContext &context, const Request &request, const std::shared_ptr<std::promise<Response>> &responsePromise)
  -> InboundRequestActivationResult
{
  const std::scoped_lock lock(inboundState_->mutex);
  if (inboundState_->shuttingDown || inboundState_->completionPool == nullptr)
  {
    return InboundRequestActivationResult::kShuttingDown;
  }

  const std::string sender = detail::senderKey(context);

  std::size_t activeInboundRequests = 0;
  for (const auto &entry : inboundState_->inboundRequestsBySender)
  {
    activeInboundRequests += entry.second.size();
  }

  if (activeInboundRequests >= options_.maxConcurrentInFlightRequests)
  {
    return InboundRequestActivationResult::kLimitExceeded;
  }

  InboundRequestState requestState;
  requestState.method = request.method;
  requestState.taskAugmented = util::cancellation::isTaskAugmentedRequest(request);
  requestState.progressToken = util::progress::extractProgressToken(request);

  auto &senderRequests = inboundState_->inboundRequestsBySender[sender];
  senderRequests[request.id] = requestState;

  if (requestState.progressToken.has_value())
  {
    auto &requestIdsByToken = inboundState_->inboundRequestIdsByProgressTokenBySender[sender];
    if (requestIdsByToken.find(*requestState.progressToken) != requestIdsByToken.end())
    {
      senderRequests.erase(request.id);
      return InboundRequestActivationResult::kDuplicateProgressToken;
    }

    requestIdsByToken[*requestState.progressToken] = request.id;
  }

  inboundState_->inboundResponsePromisesBySender[sender][request.id] = responsePromise;

  return InboundRequestActivationResult::kAccepted;
}

auto Router::completeInboundRequest(const RequestContext &context, const RequestId &requestId) -> void
{
  completeInboundRequest(inboundState_, context, requestId);
}

auto Router::completeInboundRequest(const std::shared_ptr<InboundState> &inboundState, const RequestContext &context, const RequestId &requestId) -> void
{
  const std::scoped_lock lock(inboundState->mutex);
  const std::string sender = detail::senderKey(context);

  const auto senderPromisesIt = inboundState->inboundResponsePromisesBySender.find(sender);
  if (senderPromisesIt != inboundState->inboundResponsePromisesBySender.end())
  {
    senderPromisesIt->second.erase(requestId);
    if (senderPromisesIt->second.empty())
    {
      inboundState->inboundResponsePromisesBySender.erase(senderPromisesIt);
    }
  }

  const auto senderRequestsIt = inboundState->inboundRequestsBySender.find(sender);
  if (senderRequestsIt == inboundState->inboundRequestsBySender.end())
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
    const auto senderTokensIt = inboundState->inboundRequestIdsByProgressTokenBySender.find(sender);
    if (senderTokensIt != inboundState->inboundRequestIdsByProgressTokenBySender.end())
    {
      senderTokensIt->second.erase(*requestIt->second.progressToken);
      if (senderTokensIt->second.empty())
      {
        inboundState->inboundRequestIdsByProgressTokenBySender.erase(senderTokensIt);
      }
    }
  }

  senderRequestsIt->second.erase(requestIt);
  if (senderRequestsIt->second.empty())
  {
    inboundState->inboundRequestsBySender.erase(senderRequestsIt);
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
