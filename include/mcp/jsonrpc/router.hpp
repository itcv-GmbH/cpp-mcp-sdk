#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>
#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{
namespace jsonrpc
{

using RequestHandler = std::function<std::future<Response>(const RequestContext &, const Request &)>;
using NotificationHandler = std::function<void(const RequestContext &, const Notification &)>;
using OutboundMessageSender = std::function<void(const RequestContext &, Message)>;

struct ProgressUpdate
{
  RequestId progressToken;
  double progress = 0.0;
  std::optional<double> total;
  std::optional<std::string> message;
  JsonValue additionalProperties = JsonValue::object();
};

using ProgressCallback = std::function<void(const RequestContext &, const ProgressUpdate &)>;

struct OutboundRequestOptions
{
  std::chrono::milliseconds timeout {0};
  bool cancelOnTimeout = true;
  ProgressCallback onProgress;
};

class Router
{
public:
  Router();
  ~Router() noexcept;

  Router(const Router &) = delete;
  Router(Router &&) = delete;
  auto operator=(const Router &) -> Router & = delete;
  auto operator=(Router &&) -> Router & = delete;

  auto setOutboundMessageSender(OutboundMessageSender sender) -> void;

  auto registerRequestHandler(std::string method, RequestHandler handler) -> void;
  auto registerNotificationHandler(std::string method, NotificationHandler handler) -> void;
  auto unregisterHandler(const std::string &method) -> bool;

  auto dispatchRequest(const RequestContext &context, const Request &request) const -> std::future<Response>;
  auto dispatchNotification(const RequestContext &context, const Notification &notification) const -> void;

  auto sendRequest(const RequestContext &context, Request request, OutboundRequestOptions options = {}) -> std::future<Response>;
  auto sendNotification(const RequestContext &context, Notification notification) const -> void;
  auto dispatchResponse(const RequestContext &context, const Response &response) -> bool;

  auto emitProgress(const RequestContext &context,
                    const Request &request,
                    double progress,
                    std::optional<double> total = std::nullopt,
                    std::optional<std::string> message = std::nullopt) const -> bool;

  auto emitProgress(const RequestContext &context,
                    const RequestId &progressToken,
                    double progress,
                    std::optional<double> total = std::nullopt,
                    std::optional<std::string> message = std::nullopt) const -> bool;

private:
  struct RequestIdHash
  {
    auto operator()(const RequestId &requestId) const noexcept -> std::size_t;
  };

  struct RequestIdEqual
  {
    auto operator()(const RequestId &left, const RequestId &right) const noexcept -> bool;
  };

  struct InFlightRequestState
  {
    RequestContext context;
    Request request;
    bool cancelOnTimeout = true;
    ProgressCallback progressCallback;
    std::optional<RequestId> progressToken;
    std::promise<Response> promise;
    std::shared_ptr<boost::asio::steady_timer> timeoutTimer;
  };

  enum class MarkIgnoredResponseId
  {
    kNo,
    kYes,
  };

  using RequestIdSet = std::unordered_set<RequestId, RequestIdHash, RequestIdEqual>;
  using InFlightRequestMap = std::unordered_map<RequestId, std::shared_ptr<InFlightRequestState>, RequestIdHash, RequestIdEqual>;

  auto addInFlightRequest(const std::shared_ptr<InFlightRequestState> &inFlightRequest) -> std::optional<Response>;
  auto popInFlightRequest(const RequestId &requestId, MarkIgnoredResponseId markIgnoredResponseId) -> std::shared_ptr<InFlightRequestState>;
  auto armRequestTimeout(const std::shared_ptr<InFlightRequestState> &inFlightRequest, std::chrono::milliseconds timeout) -> void;
  auto handleRequestTimeout(const RequestId &requestId) -> void;

  auto dispatchOutboundMessage(const RequestContext &context, Message message) const -> bool;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, RequestHandler> requestHandlers_;
  std::unordered_map<std::string, NotificationHandler> notificationHandlers_;
  mutable std::unordered_map<std::string, RequestIdSet> seenRequestIdsBySender_;

  OutboundMessageSender outboundMessageSender_;
  RequestIdSet seenOutboundRequestIds_;
  InFlightRequestMap inFlightRequests_;
  RequestIdSet ignoredResponseIds_;
  std::unordered_map<RequestId, RequestId, RequestIdHash, RequestIdEqual> requestIdsByProgressToken_;

  std::unique_ptr<boost::asio::thread_pool> timeoutPool_;
};

}  // namespace jsonrpc
}  // namespace mcp
