#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/server.hpp>
#include <mcp/version.hpp>

namespace mcp
{
namespace detail
{

constexpr std::string_view kInitializeMethod = "initialize";
constexpr std::string_view kPingMethod = "ping";
constexpr std::string_view kInitializedNotificationMethod = "notifications/initialized";
constexpr std::string_view kDefaultServerName = "mcp-cpp-sdk";

auto makeReadyResponseFuture(jsonrpc::Response response) -> std::future<jsonrpc::Response>
{
  std::promise<jsonrpc::Response> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

auto makePingResponse(const jsonrpc::RequestId &requestId) -> jsonrpc::Response
{
  jsonrpc::SuccessResponse response;
  response.id = requestId;
  response.result = jsonrpc::JsonValue::object();
  return response;
}

auto makeMethodNotFoundResponse(const jsonrpc::RequestId &requestId, std::string_view method) -> jsonrpc::Response
{
  const std::string message = "Method '" + std::string(method) + "' is not available for the negotiated server capabilities.";
  return jsonrpc::makeErrorResponse(jsonrpc::makeMethodNotFoundError(std::nullopt, message), requestId);
}

auto makeLifecycleRejectedResponse(const jsonrpc::RequestId &requestId, std::string_view method) -> jsonrpc::Response
{
  const std::string message = "Method '" + std::string(method) + "' is not valid before initialization completes.";
  return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidRequestError(std::nullopt, message), requestId);
}

auto defaultServerInfo() -> Implementation
{
  return {std::string(detail::kDefaultServerName), std::string(kSdkVersion)};
}

auto capabilityForMethod(std::string_view method) -> std::optional<std::string_view>
{
  if (method == "tools/list" || method == "tools/call")
  {
    return "tools";
  }

  if (method == "resources/list" || method == "resources/read" || method == "resources/templates/list" || method == "resources/subscribe" || method == "resources/unsubscribe")
  {
    return "resources";
  }

  if (method == "prompts/list" || method == "prompts/get")
  {
    return "prompts";
  }

  if (method == "completion/complete")
  {
    return "completions";
  }

  if (method == "logging/setLevel")
  {
    return "logging";
  }

  if (method == "tasks/get" || method == "tasks/result" || method == "tasks/list" || method == "tasks/cancel")
  {
    return "tasks";
  }

  return std::nullopt;
}

}  // namespace detail

auto Server::create(SessionOptions options) -> std::shared_ptr<Server>
{
  ServerConfiguration configuration;
  configuration.sessionOptions = std::move(options);
  return create(std::move(configuration));
}

auto Server::create(ServerConfiguration configuration) -> std::shared_ptr<Server>
{
  auto session = std::make_shared<Session>(configuration.sessionOptions);
  return std::make_shared<Server>(std::move(session), std::move(configuration));
}

Server::Server(std::shared_ptr<Session> session)
  : Server(std::move(session), ServerConfiguration {})
{
}

Server::Server(std::shared_ptr<Session> session, ServerConfiguration configuration)
  : session_(std::move(session))
  , configuration_(std::move(configuration))
{
  if (!session_)
  {
    throw std::invalid_argument("Server requires a non-null session");
  }

  session_->setRole(SessionRole::kServer);

  if (!configuration_.serverInfo.has_value())
  {
    configuration_.serverInfo = detail::defaultServerInfo();
  }

  configureSessionInitialization();
  registerCoreHandlers();
}

auto Server::configuration() const noexcept -> const ServerConfiguration &
{
  return configuration_;
}

auto Server::session() const noexcept -> const std::shared_ptr<Session> &
{
  return session_;
}

auto Server::start() -> void
{
  session_->setRole(SessionRole::kServer);
  session_->start();
}

auto Server::stop() -> void
{
  session_->stop();
}

auto Server::registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void
{
  if (handler == nullptr)
  {
    throw std::invalid_argument("Request handler must not be null");
  }

  if (isCoreRequestMethod(method))
  {
    throw std::invalid_argument("Core request handlers are managed by Server");
  }

  const std::string methodName = method;
  router_.registerRequestHandler(std::move(method),
                                 [this, methodName, handler = std::move(handler)](
                                   const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>  // NOLINT(bugprone-exception-escape)
                                 {
                                   if (!isMethodEnabledByCapability(methodName))
                                   {
                                     return detail::makeReadyResponseFuture(detail::makeMethodNotFoundResponse(request.id, methodName));
                                   }

                                   return handler(context, request);
                                 });
}

auto Server::registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void
{
  if (handler == nullptr)
  {
    throw std::invalid_argument("Notification handler must not be null");
  }

  if (method == detail::kInitializedNotificationMethod)
  {
    throw std::invalid_argument("Core notification handlers are managed by Server");
  }

  router_.registerNotificationHandler(std::move(method), std::move(handler));
}

auto Server::setOutboundMessageSender(jsonrpc::OutboundMessageSender sender) -> void
{
  router_.setOutboundMessageSender(std::move(sender));
}

auto Server::handleRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
{
  if (!session_->canHandleRequest(request.method))
  {
    return detail::makeReadyResponseFuture(detail::makeLifecycleRejectedResponse(request.id, request.method));
  }

  if (!isMethodEnabledByCapability(request.method))
  {
    return detail::makeReadyResponseFuture(detail::makeMethodNotFoundResponse(request.id, request.method));
  }

  return router_.dispatchRequest(context, request);
}

auto Server::handleNotification(const jsonrpc::RequestContext &context, const jsonrpc::Notification &notification) -> void
{
  router_.dispatchNotification(context, notification);
}

auto Server::handleResponse(const jsonrpc::RequestContext &context, const jsonrpc::Response &response) -> bool
{
  return router_.dispatchResponse(context, response);
}

auto Server::sendRequest(const jsonrpc::RequestContext &context, jsonrpc::Request request, jsonrpc::OutboundRequestOptions options) -> std::future<jsonrpc::Response>
{
  return router_.sendRequest(context, std::move(request), std::move(options));
}

auto Server::sendNotification(const jsonrpc::RequestContext &context, jsonrpc::Notification notification) -> void
{
  router_.sendNotification(context, std::move(notification));
}

auto Server::configureSessionInitialization() -> void
{
  if (!configuration_.serverInfo.has_value())
  {
    configuration_.serverInfo = detail::defaultServerInfo();
  }

  session_->configureServerInitialization(configuration_.capabilities, *configuration_.serverInfo, configuration_.instructions);
}

auto Server::registerCoreHandlers() -> void
{
  router_.registerRequestHandler(std::string(detail::kInitializeMethod),
                                 [this](const jsonrpc::RequestContext &, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(session_->handleInitializeRequest(request)); });

  router_.registerRequestHandler(std::string(detail::kPingMethod),
                                 [](const jsonrpc::RequestContext &, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(detail::makePingResponse(request.id)); });

  router_.registerNotificationHandler(std::string(detail::kInitializedNotificationMethod),
                                      [this](const jsonrpc::RequestContext &, const jsonrpc::Notification &) -> void { session_->handleInitializedNotification(); });
}

auto Server::isMethodEnabledByCapability(std::string_view method) const -> bool
{
  const auto capability = detail::capabilityForMethod(method);
  if (!capability.has_value())
  {
    return true;
  }

  return configuration_.capabilities.hasCapability(*capability);
}

auto Server::isCoreRequestMethod(std::string_view method) -> bool
{
  return method == detail::kInitializeMethod || method == detail::kPingMethod;
}

}  // namespace mcp
