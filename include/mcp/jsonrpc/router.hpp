#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>
#include <mcp/export.hpp>
#include <mcp/jsonrpc/handler_types.hpp>
#include <mcp/jsonrpc/message_functions.hpp>
#include <mcp/jsonrpc/outbound_request_options.hpp>
#include <mcp/jsonrpc/request.hpp>
#include <mcp/jsonrpc/request_context.hpp>
#include <mcp/jsonrpc/router_enums.hpp>
#include <mcp/jsonrpc/router_options.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief JSON-RPC message routing and dispatch.
 *
 * @section Thread Safety
 *
 * @par Thread-Safety Classification: Thread-safe
 *
 * The Router class provides thread-safe access to all its public methods.
 * Internal synchronization uses separate mutexes for inbound and outbound state.
 *
 * @par Thread-Safe Methods (concurrent invocation allowed):
 * - Router(), ~Router()
 * - setOutboundMessageSender()
 * - registerRequestHandler(), registerNotificationHandler(), unregisterHandler()
 * - dispatchRequest(), dispatchNotification()
 * - sendRequest(), sendNotification(), dispatchResponse()
 * - emitProgress() (both overloads)
 *
 * @par Concurrency Rules:
 * 1. Handler registration methods may be called at any time, but handlers set after
 *    routing begins may miss early messages.
 * 2. setOutboundMessageSender() must be called before dispatching messages.
 * 3. Progress callbacks are invoked directly on the router/I/O thread. They must be
 *    fast and non-blocking.
 * 4. All public methods may be called concurrently from multiple threads without
 *    external synchronization.
 * 5. Concurrent sendRequest() calls are supported and responses are correctly routed
 *    to their respective futures regardless of completion order.
 *
 * @par Internal Lock Ordering:
 * The Router maintains two separate mutex domains:
 * 1. Outbound mutex (mutex_): Protects outbound request state, handler maps, and
 *    outbound message sender
 * 2. Inbound state mutex (inboundState_->mutex): Protects inbound request state,
 *    response promises, and completion pool
 *
 * Lock ordering when both are needed: acquire outbound mutex first, then inbound
 * state mutex.
 *
 * @par Callback Threading Rules:
 * Note: Callbacks are invoked on the router/I/O thread. They must be fast and non-blocking.
 * - RequestHandler: Serial invocation per request, router/I/O thread
 * - NotificationHandler: Serial invocation per notification, router/I/O thread
 * - OutboundMessageSender: Serial invocation per message, threading determined by caller
 * - ProgressCallback: Serial invocation per progress token, router/I/O thread
 *
 * @section Exceptions
 *
 * The Router class provides the following exception behavior:
 *
 * @subsection Construction and Destruction
 * - Router(RouterOptions) does not throw
 * - ~Router() is declared noexcept and will not throw even with in-flight requests
 *
 * @subsection Handler Registration
 * - registerRequestHandler(), registerNotificationHandler(), unregisterHandler() do not throw
 *
 * @subsection Dispatch Operations
 * - dispatchRequest() catches exceptions from user-provided RequestHandler callbacks and
 *   converts them to JSON-RPC error responses with code -32603 (Internal Error)
 * - dispatchNotification() catches exceptions from user-provided NotificationHandler and
 *   ProgressCallback; exceptions are contained and reported via the error reporter
 * - dispatchResponse() returns bool; does not throw directly
 *
 * @subsection Request Operations
 * - sendRequest() may throw std::runtime_error on transport or serialization failure
 * - sendNotification() returns void; does not throw directly
 *
 * @subsection Progress Operations
 * - emitProgress() returns bool; does not throw directly
 *
 * @subsection Background Thread Behavior (No-Throw Guarantee)
 * All work posted to internal thread pools (completion pool, timeout pool) is wrapped
 * in exception boundaries that:
 * 1. Catch all exceptions thrown by the work item
 * 2. Report failures via the unified error reporting mechanism (ErrorReporter)
 * 3. Never allow exceptions to propagate and terminate the worker thread
 *
 * User-provided callbacks (RequestHandler, NotificationHandler, ProgressCallback) that
 * throw exceptions will have those exceptions caught and reported via the error reporter
 * configured in RouterOptions. The router continues operating after reporting the error.
 *
 * @subsection Shutdown Behavior
 * When the router is destroyed:
 * - All in-flight outbound requests receive an internal error response
 * - All pending inbound request handlers have their promises completed with an error
 * - Shutdown is guaranteed not to throw exceptions (destructor is noexcept)
 * - All futures associated with in-flight requests will be resolved (either successfully
 *   or with an error) before destruction completes
 */
class MCP_SDK_EXPORT Router
{
public:
  explicit Router(RouterOptions options = {});
  ~Router() noexcept;

  Router(const Router &) = delete;
  Router(Router &&) = delete;
  auto operator=(const Router &) -> Router & = delete;
  auto operator=(Router &&) -> Router & = delete;

  auto setOutboundMessageSender(OutboundMessageSender sender) -> void;

  auto registerRequestHandler(std::string method, RequestHandler handler) -> void;
  auto registerNotificationHandler(std::string method, NotificationHandler handler) -> void;
  auto unregisterHandler(const std::string &method) -> bool;

  auto dispatchRequest(const RequestContext &context, const Request &request) -> std::future<Response>;
  auto dispatchNotification(const RequestContext &context, const Notification &notification) -> void;

  auto sendRequest(const RequestContext &context, Request request, OutboundRequestOptions options = {}) -> std::future<Response>;
  auto sendNotification(const RequestContext &context, Notification notification) const -> void;
  auto dispatchResponse(const RequestContext &context, const Response &response) -> bool;

  auto emitProgress(
    const RequestContext &context, const Request &request, double progress, std::optional<double> total = std::nullopt, std::optional<std::string> message = std::nullopt) -> bool;

  auto emitProgress(const RequestContext &context,
                    const RequestId &progressToken,
                    double progress,
                    std::optional<double> total = std::nullopt,
                    std::optional<std::string> message = std::nullopt) -> bool;

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
    bool taskAugmented = false;
    std::optional<std::string> taskId;
    std::optional<double> lastObservedProgress;
    std::promise<Response> promise;
    std::shared_ptr<boost::asio::steady_timer> timeoutTimer;
  };

  struct InboundRequestState
  {
    std::string method;
    bool taskAugmented = false;
    std::optional<RequestId> progressToken;
    std::optional<double> lastEmittedProgress;
    bool cancelled = false;
  };

  using RequestIdSet = std::unordered_set<RequestId, RequestIdHash, RequestIdEqual>;
  using InFlightRequestMap = std::unordered_map<RequestId, std::shared_ptr<InFlightRequestState>, RequestIdHash, RequestIdEqual>;
  using InboundRequestMap = std::unordered_map<RequestId, InboundRequestState, RequestIdHash, RequestIdEqual>;
  using RequestIdByProgressTokenMap = std::unordered_map<RequestId, RequestId, RequestIdHash, RequestIdEqual>;
  using PendingTaskCancellationMap = std::unordered_map<RequestId, RequestContext, RequestIdHash, RequestIdEqual>;

  using InboundResponsePromiseMap = std::unordered_map<RequestId, std::shared_ptr<std::promise<Response>>, RequestIdHash, RequestIdEqual>;
  using InboundResponsePromiseBySenderMap = std::unordered_map<std::string, InboundResponsePromiseMap>;

  struct InboundState
  {
    std::mutex mutex;
    bool shuttingDown = false;
    std::unordered_map<std::string, InboundRequestMap> inboundRequestsBySender;
    std::unordered_map<std::string, RequestIdByProgressTokenMap> inboundRequestIdsByProgressTokenBySender;
    InboundResponsePromiseBySenderMap inboundResponsePromisesBySender;
    // Shared pointer to completion thread_pool with custom deleter that posts
    // deletion to a dedicated thread to avoid destructor running on a worker thread.
    std::shared_ptr<boost::asio::thread_pool> completionPool;
  };

  auto addInFlightRequest(const std::shared_ptr<InFlightRequestState> &inFlightRequest) -> std::optional<Response>;
  auto popInFlightRequest(const RequestId &requestId, MarkIgnoredResponseId markIgnoredResponseId) -> std::shared_ptr<InFlightRequestState>;
  auto armRequestTimeout(const std::shared_ptr<InFlightRequestState> &inFlightRequest, std::chrono::milliseconds timeout) -> void;
  auto handleRequestTimeout(const RequestId &requestId) -> void;
  static auto completeInboundRequest(const std::shared_ptr<InboundState> &inboundState, const RequestContext &context, const RequestId &requestId) -> void;

  auto markInboundRequestActive(const RequestContext &context, const Request &request, const std::shared_ptr<std::promise<Response>> &responsePromise)
    -> InboundRequestActivationResult;
  auto completeInboundRequest(const RequestContext &context, const RequestId &requestId) -> void;

  auto dispatchOutboundMessage(const RequestContext &context, Message message) const -> bool;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, RequestHandler> requestHandlers_;
  std::unordered_map<std::string, NotificationHandler> notificationHandlers_;
  mutable std::unordered_map<std::string, RequestIdSet> seenRequestIdsBySender_;

  OutboundMessageSender outboundMessageSender_;
  RequestIdSet seenOutboundRequestIds_;
  InFlightRequestMap inFlightRequests_;
  RequestIdSet ignoredResponseIds_;
  PendingTaskCancellationMap pendingTaskCancellationByRequestId_;
  RequestIdByProgressTokenMap requestIdsByProgressToken_;
  std::shared_ptr<InboundState> inboundState_;

  RouterOptions options_;

  std::unique_ptr<boost::asio::thread_pool> timeoutPool_;
};

}  // namespace mcp::jsonrpc
