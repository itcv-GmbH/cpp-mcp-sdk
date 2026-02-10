#pragma once

#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{
namespace jsonrpc
{

using RequestHandler = std::function<std::future<Response>(const RequestContext &, const Request &)>;
using NotificationHandler = std::function<void(const RequestContext &, const Notification &)>;

class Router
{
public:
  auto registerRequestHandler(std::string method, RequestHandler handler) -> void;
  auto registerNotificationHandler(std::string method, NotificationHandler handler) -> void;
  auto unregisterHandler(const std::string &method) -> bool;

  auto dispatchRequest(const RequestContext &context, const Request &request) const -> std::future<Response>;
  auto dispatchNotification(const RequestContext &context, const Notification &notification) const -> void;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, RequestHandler> requestHandlers_;
  std::unordered_map<std::string, NotificationHandler> notificationHandlers_;
  mutable std::unordered_map<std::string, std::unordered_set<std::string>> seenRequestIdsBySender_;
};

}  // namespace jsonrpc
}  // namespace mcp
