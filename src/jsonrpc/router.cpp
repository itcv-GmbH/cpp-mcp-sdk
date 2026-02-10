#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>

namespace mcp::jsonrpc
{
namespace detail
{

static auto requestIdKey(const RequestId &id) -> std::string
{
  if (std::holds_alternative<std::int64_t>(id))
  {
    return "i:" + std::to_string(std::get<std::int64_t>(id));
  }

  return "s:" + std::get<std::string>(id);
}

static auto senderKey(const RequestContext &context) -> std::string
{
  if (context.sessionId.has_value())
  {
    return *context.sessionId;
  }

  return "__default_sender__";
}

template<typename T>
static auto makeReadyFuture(T value) -> std::future<T>
{
  std::promise<T> promise;
  promise.set_value(std::move(value));
  return promise.get_future();
}

}  // namespace detail

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
    const std::string requestId = detail::requestIdKey(request.id);
    auto &senderIds = seenRequestIdsBySender_[sender];
    const bool isNewId = senderIds.insert(requestId).second;
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
  NotificationHandler handler;

  {
    const std::scoped_lock lock(mutex_);

    const auto notificationHandlerIt = notificationHandlers_.find(notification.method);
    if (notificationHandlerIt == notificationHandlers_.end())
    {
      return;
    }

    handler = notificationHandlerIt->second;
  }

  handler(context, notification);
}

}  // namespace mcp::jsonrpc
