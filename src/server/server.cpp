#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "mcp/server/server.hpp"

#include "mcp/detail/thread_boundary.hpp"
#include "mcp/jsonrpc/error_factories.hpp"
#include "mcp/jsonrpc/handler_types.hpp"
#include "mcp/jsonrpc/notification.hpp"
#include "mcp/jsonrpc/outbound_request_options.hpp"
#include "mcp/jsonrpc/request.hpp"
#include "mcp/jsonrpc/request_context.hpp"
#include "mcp/jsonrpc/response.hpp"
#include "mcp/jsonrpc/response_factories.hpp"
#include "mcp/jsonrpc/router_options.hpp"
#include "mcp/jsonrpc/success_response.hpp"
#include "mcp/jsonrpc/types.hpp"
#include "mcp/lifecycle/session.hpp"
#include "mcp/lifecycle/session/implementation.hpp"
#include "mcp/lifecycle/session/request_options.hpp"
#include "mcp/lifecycle/session/session_options.hpp"
#include "mcp/lifecycle/session/session_role.hpp"
#include "mcp/schema/format_diagnostics.hpp"
#include "mcp/schema/tool_schema_kind.hpp"
#include "mcp/schema/validation_result.hpp"
#include "mcp/schema/validator.hpp"
#include "mcp/sdk/error_reporter.hpp"
#include "mcp/server/completion_request.hpp"
#include "mcp/server/completion_result.hpp"
#include "mcp/server/detail/helpers.hpp"
#include "mcp/server/list_endpoint.hpp"
#include "mcp/server/log_level.hpp"
#include "mcp/server/pagination_window.hpp"
#include "mcp/server/prompt_definition.hpp"
#include "mcp/server/prompt_get_context.hpp"
#include "mcp/server/prompt_get_result.hpp"
#include "mcp/server/prompt_handler.hpp"
#include "mcp/server/registered_prompt.hpp"
#include "mcp/server/registered_resource.hpp"
#include "mcp/server/registered_tool.hpp"
#include "mcp/server/resource_content.hpp"
#include "mcp/server/resource_content_kind.hpp"
#include "mcp/server/resource_definition.hpp"
#include "mcp/server/resource_read_context.hpp"
#include "mcp/server/resource_read_handler.hpp"
#include "mcp/server/resource_subscription.hpp"
#include "mcp/server/resource_template_definition.hpp"
#include "mcp/server/server_configuration.hpp"
#include "mcp/server/tool_definition.hpp"
#include "mcp/server/tool_handler.hpp"
#include "mcp/util/create_task_result.hpp"
#include "mcp/util/in_memory_task_store.hpp"
#include "mcp/util/task.hpp"
#include "mcp/util/task_augmentation_request.hpp"
#include "mcp/util/task_receiver.hpp"
#include "mcp/util/task_status.hpp"

namespace mcp::server
{

auto ResourceContent::text(std::string uri,
                           std::string text,
                           std::optional<std::string> mimeType,
                           std::optional<jsonrpc::JsonValue> annotations,
                           std::optional<jsonrpc::JsonValue> metadata) -> ResourceContent
{
  ResourceContent content;
  content.uri = std::move(uri);
  content.mimeType = std::move(mimeType);
  content.kind = ResourceContentKind::kText;
  content.value = std::move(text);
  content.annotations = std::move(annotations);
  content.metadata = std::move(metadata);
  return content;
}

auto ResourceContent::blobBase64(std::string uri,
                                 std::string blobBase64,
                                 std::optional<std::string> mimeType,
                                 std::optional<jsonrpc::JsonValue> annotations,
                                 std::optional<jsonrpc::JsonValue> metadata) -> ResourceContent
{
  ResourceContent content;
  content.uri = std::move(uri);
  content.mimeType = std::move(mimeType);
  content.kind = ResourceContentKind::kBlobBase64;
  content.value = std::move(blobBase64);
  content.annotations = std::move(annotations);
  content.metadata = std::move(metadata);
  return content;
}

auto ResourceContent::blobBytes(std::string uri,
                                const std::vector<std::uint8_t> &blobBytes,
                                std::optional<std::string> mimeType,
                                std::optional<jsonrpc::JsonValue> annotations,
                                std::optional<jsonrpc::JsonValue> metadata) -> ResourceContent
{
  const std::string byteView(blobBytes.begin(), blobBytes.end());
  return blobBase64(std::move(uri), server::detail::encodeStandardBase64(byteView), std::move(mimeType), std::move(annotations), std::move(metadata));
}

}  // namespace mcp::server

namespace mcp::server
{

struct Server::TaskStatusObserverState
{
  std::weak_ptr<lifecycle::Session> session;
  bool emitTaskStatusNotifications = false;
  mutable std::mutex mutex;
  jsonrpc::OutboundMessageSender outboundMessageSender;
};

auto Server::create(lifecycle::session::SessionOptions options) -> std::shared_ptr<Server>
{
  ServerConfiguration configuration;
  configuration.sessionOptions = std::move(options);
  return create(std::move(configuration));
}

auto Server::create(ServerConfiguration configuration) -> std::shared_ptr<Server>
{
  auto session = std::make_shared<lifecycle::Session>(configuration.sessionOptions);
  return std::make_shared<Server>(std::move(session), std::move(configuration));
}

Server::Server(std::shared_ptr<lifecycle::Session> session)
  : Server(std::move(session), ServerConfiguration {})
{
}

Server::Server(std::shared_ptr<lifecycle::Session> session, ServerConfiguration configuration)
  : session_(std::move(session))
  , configuration_(std::move(configuration))
  , router_(
      [errorReporter = configuration_.sessionOptions.errorReporter]() -> jsonrpc::RouterOptions
      {
        jsonrpc::RouterOptions opts;
        opts.errorReporter = errorReporter;
        return opts;
      }())
{
  if (!session_)
  {
    throw std::invalid_argument("Server requires a non-null session");
  }

  session_->setRole(lifecycle::session::SessionRole::kServer);

  if (!configuration_.serverInfo.has_value())
  {
    configuration_.serverInfo = detail::defaultServerInfo();
  }

  if (!configuration_.taskStore)
  {
    configuration_.taskStore = std::make_shared<util::InMemoryTaskStore>();
  }

  taskStatusObserverState_ = std::make_shared<TaskStatusObserverState>();
  taskStatusObserverState_->session = session_;
  taskStatusObserverState_->emitTaskStatusNotifications = configuration_.emitTaskStatusNotifications;

  taskReceiver_ = std::make_shared<util::TaskReceiver>(configuration_.taskStore, configuration_.defaultTaskPollInterval, detail::kTasksPageSize);
  taskReceiver_->setStatusObserver(
    [weakObserverState = std::weak_ptr<TaskStatusObserverState>(taskStatusObserverState_)](const jsonrpc::RequestContext &context, const util::Task &task) -> void
    {
      const std::shared_ptr<TaskStatusObserverState> observerState = weakObserverState.lock();
      if (!observerState || !observerState->emitTaskStatusNotifications)
      {
        return;
      }

      const std::shared_ptr<lifecycle::Session> session = observerState->session.lock();
      if (!session || !session->canSendNotification(detail::kTasksStatusNotificationMethod))
      {
        return;
      }

      jsonrpc::OutboundMessageSender outboundMessageSender;
      {
        const std::scoped_lock lock(observerState->mutex);
        outboundMessageSender = observerState->outboundMessageSender;
      }

      if (!outboundMessageSender)
      {
        return;
      }

      jsonrpc::Notification notification;
      notification.method = std::string(detail::kTasksStatusNotificationMethod);
      notification.params = util::taskToJson(task);

      try
      {
        outboundMessageSender(context, jsonrpc::Message {std::move(notification)});
      }
      catch (...)
      {
        return;
      }
    });

  configureSessionInitialization();
  registerCoreHandlers();
}

auto Server::configuration() const noexcept -> const ServerConfiguration &
{
  return configuration_;
}

auto Server::session() const noexcept -> const std::shared_ptr<lifecycle::Session> &
{
  return session_;
}

auto Server::start() -> void
{
  session_->setRole(lifecycle::session::SessionRole::kServer);
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
                                 [this, methodName, handler = std::move(handler)](  // NOLINT(bugprone-exception-escape)
                                   const jsonrpc::RequestContext &context,
                                   const jsonrpc::Request &request) -> std::future<jsonrpc::Response>  // NOLINT(bugprone-exception-escape)
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
  if (taskStatusObserverState_)
  {
    const std::scoped_lock lock(taskStatusObserverState_->mutex);
    taskStatusObserverState_->outboundMessageSender = sender;
  }

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
  const jsonrpc::JsonValue lifecycleParams = request.params.has_value() ? *request.params : jsonrpc::JsonValue::object();
  lifecycle::session::RequestOptions requestOptions;
  requestOptions.timeout = options.timeout;
  requestOptions.cancelOnTimeout = options.cancelOnTimeout;
  session_->enforceOutboundRequestLifecycle(request.method, lifecycleParams, requestOptions);
  return router_.sendRequest(context, std::move(request), std::move(options));
}

auto Server::sendNotification(const jsonrpc::RequestContext &context, jsonrpc::Notification notification) -> void
{
  router_.sendNotification(context, std::move(notification));
}

auto Server::setCompletionHandler(CompletionHandler handler) -> void
{
  const std::scoped_lock lock(utilityMutex_);
  completionHandler_ = std::move(handler);
}

auto Server::registerTool(ToolDefinition definition, ToolHandler handler) -> void
{
  if (handler == nullptr)
  {
    throw std::invalid_argument("Tool handler must not be null");
  }

  if (definition.name.empty())
  {
    throw std::invalid_argument("Tool name must not be empty");
  }

  if (!definition.inputSchema.is_object())
  {
    throw std::invalid_argument("Tool input schema must be a JSON object");
  }

  const schema::ValidationResult inputSchemaValidation = detail::mcpSchemaValidator().validateToolSchema(definition.inputSchema, schema::ToolSchemaKind::kInput);
  if (!inputSchemaValidation.valid)
  {
    throw std::invalid_argument("Tool input schema is invalid: " + schema::formatDiagnostics(inputSchemaValidation));
  }

  if (definition.outputSchema.has_value())
  {
    const schema::ValidationResult outputSchemaValidation = detail::mcpSchemaValidator().validateToolSchema(*definition.outputSchema, schema::ToolSchemaKind::kOutput);
    if (!outputSchemaValidation.valid)
    {
      throw std::invalid_argument("Tool output schema is invalid: " + schema::formatDiagnostics(outputSchemaValidation));
    }
  }

  {
    const std::scoped_lock lock(toolsMutex_);
    const auto existingTool =
      std::find_if(tools_.begin(), tools_.end(), [&definition](const RegisteredTool &registeredTool) -> bool { return registeredTool.definition.name == definition.name; });
    if (existingTool != tools_.end())
    {
      throw std::invalid_argument("Tool already registered: " + definition.name);
    }

    tools_.push_back(RegisteredTool {std::move(definition), std::move(handler)});
  }

  static_cast<void>(notifyToolsListChanged());
}

auto Server::unregisterTool(std::string_view name) -> bool
{
  bool removed = false;

  {
    const std::scoped_lock lock(toolsMutex_);
    const auto iter = std::find_if(tools_.begin(), tools_.end(), [name](const RegisteredTool &registeredTool) -> bool { return registeredTool.definition.name == name; });

    if (iter != tools_.end())
    {
      tools_.erase(iter);
      removed = true;
    }
  }

  if (removed)
  {
    static_cast<void>(notifyToolsListChanged());
  }

  return removed;
}

auto Server::notifyToolsListChanged(const jsonrpc::RequestContext &context) -> bool
{
  if (!configuration_.capabilities.tools().has_value() || !configuration_.capabilities.tools()->listChanged)
  {
    return false;
  }

  if (!session_->canSendNotification(detail::kToolsListChangedNotificationMethod))
  {
    return false;
  }

  jsonrpc::Notification notification;
  notification.method = std::string(detail::kToolsListChangedNotificationMethod);
  notification.params = jsonrpc::JsonValue::object();
  sendNotification(context, std::move(notification));
  return true;
}

auto Server::registerResource(ResourceDefinition definition, ResourceReadHandler handler) -> void
{
  if (handler == nullptr)
  {
    throw std::invalid_argument("Resource handler must not be null");
  }

  if (definition.uri.empty())
  {
    throw std::invalid_argument("Resource URI must not be empty");
  }

  if (definition.name.empty())
  {
    throw std::invalid_argument("Resource name must not be empty");
  }

  {
    const std::scoped_lock lock(resourcesMutex_);
    const auto existingResource = std::find_if(
      resources_.begin(), resources_.end(), [&definition](const RegisteredResource &registeredResource) -> bool { return registeredResource.definition.uri == definition.uri; });
    if (existingResource != resources_.end())
    {
      throw std::invalid_argument("Resource already registered: " + definition.uri);
    }

    resources_.push_back(RegisteredResource {std::move(definition), std::move(handler)});
  }

  static_cast<void>(notifyResourcesListChanged());
}

auto Server::unregisterResource(std::string_view uri) -> bool
{
  bool removed = false;

  {
    const std::scoped_lock lock(resourcesMutex_);
    const auto resourceIter =
      std::find_if(resources_.begin(), resources_.end(), [uri](const RegisteredResource &registeredResource) -> bool { return registeredResource.definition.uri == uri; });

    if (resourceIter != resources_.end())
    {
      resources_.erase(resourceIter);

      resourceSubscriptions_.erase(
        std::remove_if(resourceSubscriptions_.begin(), resourceSubscriptions_.end(), [uri](const ResourceSubscription &subscription) -> bool { return subscription.uri == uri; }),
        resourceSubscriptions_.end());
      removed = true;
    }
  }

  if (removed)
  {
    static_cast<void>(notifyResourcesListChanged());
  }

  return removed;
}

auto Server::registerResourceTemplate(ResourceTemplateDefinition definition) -> void
{
  if (definition.uriTemplate.empty())
  {
    throw std::invalid_argument("Resource template URI template must not be empty");
  }

  if (definition.name.empty())
  {
    throw std::invalid_argument("Resource template name must not be empty");
  }

  {
    const std::scoped_lock lock(resourcesMutex_);
    const auto existingTemplate =
      std::find_if(resourceTemplates_.begin(),
                   resourceTemplates_.end(),
                   [&definition](const ResourceTemplateDefinition &templateDefinition) -> bool { return templateDefinition.uriTemplate == definition.uriTemplate; });
    if (existingTemplate != resourceTemplates_.end())
    {
      throw std::invalid_argument("Resource template already registered: " + definition.uriTemplate);
    }

    resourceTemplates_.push_back(std::move(definition));
  }

  static_cast<void>(notifyResourcesListChanged());
}

auto Server::unregisterResourceTemplate(std::string_view uriTemplate) -> bool
{
  bool removed = false;

  {
    const std::scoped_lock lock(resourcesMutex_);
    const auto templateIter = std::find_if(resourceTemplates_.begin(),
                                           resourceTemplates_.end(),
                                           [uriTemplate](const ResourceTemplateDefinition &templateDefinition) -> bool { return templateDefinition.uriTemplate == uriTemplate; });
    if (templateIter != resourceTemplates_.end())
    {
      resourceTemplates_.erase(templateIter);
      removed = true;
    }
  }

  if (removed)
  {
    static_cast<void>(notifyResourcesListChanged());
  }

  return removed;
}

auto Server::registerPrompt(PromptDefinition definition, PromptHandler handler) -> void
{
  if (handler == nullptr)
  {
    throw std::invalid_argument("Prompt handler must not be null");
  }

  if (definition.name.empty())
  {
    throw std::invalid_argument("Prompt name must not be empty");
  }

  for (const auto &argument : definition.arguments)
  {
    if (argument.name.empty())
    {
      throw std::invalid_argument("Prompt argument name must not be empty");
    }
  }

  {
    const std::scoped_lock lock(promptsMutex_);
    const auto existingPrompt = std::find_if(
      prompts_.begin(), prompts_.end(), [&definition](const RegisteredPrompt &registeredPrompt) -> bool { return registeredPrompt.definition.name == definition.name; });
    if (existingPrompt != prompts_.end())
    {
      throw std::invalid_argument("Prompt already registered: " + definition.name);
    }

    for (std::size_t argumentIndex = 0; argumentIndex < definition.arguments.size(); ++argumentIndex)
    {
      const auto duplicate =
        std::find_if(definition.arguments.begin() + static_cast<std::ptrdiff_t>(argumentIndex + 1),
                     definition.arguments.end(),
                     [&definition, argumentIndex](const PromptArgumentDefinition &argument) -> bool { return argument.name == definition.arguments[argumentIndex].name; });
      if (duplicate != definition.arguments.end())
      {
        throw std::invalid_argument("Prompt argument names must be unique: " + definition.arguments[argumentIndex].name);
      }
    }

    prompts_.push_back(RegisteredPrompt {std::move(definition), std::move(handler)});
  }

  static_cast<void>(notifyPromptsListChanged());
}

auto Server::unregisterPrompt(std::string_view name) -> bool
{
  bool removed = false;

  {
    const std::scoped_lock lock(promptsMutex_);
    const auto promptIter =
      std::find_if(prompts_.begin(), prompts_.end(), [name](const RegisteredPrompt &registeredPrompt) -> bool { return registeredPrompt.definition.name == name; });
    if (promptIter != prompts_.end())
    {
      prompts_.erase(promptIter);
      removed = true;
    }
  }

  if (removed)
  {
    static_cast<void>(notifyPromptsListChanged());
  }

  return removed;
}

auto Server::notifyPromptsListChanged(const jsonrpc::RequestContext &context) -> bool
{
  if (!configuration_.capabilities.prompts().has_value() || !configuration_.capabilities.prompts()->listChanged)
  {
    return false;
  }

  if (!session_->canSendNotification(detail::kPromptsListChangedNotificationMethod))
  {
    return false;
  }

  jsonrpc::Notification notification;
  notification.method = std::string(detail::kPromptsListChangedNotificationMethod);
  notification.params = jsonrpc::JsonValue::object();
  sendNotification(context, std::move(notification));
  return true;
}

auto Server::notifyResourceUpdated(std::string uri, const jsonrpc::RequestContext &context) -> bool
{
  if (!configuration_.capabilities.resources().has_value() || !configuration_.capabilities.resources()->subscribe)
  {
    return false;
  }

  const std::string sessionKey = detail::sessionKeyForContext(context);

  {
    const std::scoped_lock lock(resourcesMutex_);
    const auto subscribed =
      std::find_if(resourceSubscriptions_.begin(),
                   resourceSubscriptions_.end(),
                   [&sessionKey, &uri](const ResourceSubscription &subscription) -> bool { return subscription.sessionKey == sessionKey && subscription.uri == uri; });
    if (subscribed == resourceSubscriptions_.end())
    {
      return false;
    }
  }

  if (!session_->canSendNotification(detail::kResourcesUpdatedNotificationMethod))
  {
    return false;
  }

  jsonrpc::Notification notification;
  notification.method = std::string(detail::kResourcesUpdatedNotificationMethod);
  notification.params = jsonrpc::JsonValue::object();
  (*notification.params)["uri"] = uri;
  sendNotification(context, std::move(notification));
  return true;
}

auto Server::notifyResourcesListChanged(const jsonrpc::RequestContext &context) -> bool
{
  if (!configuration_.capabilities.resources().has_value() || !configuration_.capabilities.resources()->listChanged)
  {
    return false;
  }

  if (!session_->canSendNotification(detail::kResourcesListChangedNotificationMethod))
  {
    return false;
  }

  jsonrpc::Notification notification;
  notification.method = std::string(detail::kResourcesListChangedNotificationMethod);
  notification.params = jsonrpc::JsonValue::object();
  sendNotification(context, std::move(notification));
  return true;
}

auto Server::emitLogMessage(const jsonrpc::RequestContext &context, LogLevel level, jsonrpc::JsonValue data, std::optional<std::string> logger) -> bool
{
  if (!configuration_.capabilities.hasCapability("logging"))
  {
    return false;
  }

  LogLevel configuredLevel = LogLevel::kDebug;
  {
    const std::scoped_lock lock(utilityMutex_);
    configuredLevel = logLevel_;
  }

  if (detail::logLevelWeight(level) < detail::logLevelWeight(configuredLevel))
  {
    return false;
  }

  jsonrpc::Notification notification;
  notification.method = std::string(detail::kMessageNotificationMethod);
  notification.params = jsonrpc::JsonValue::object();
  (*notification.params)["level"] = std::string(detail::logLevelToString(level));
  if (logger.has_value())
  {
    (*notification.params)["logger"] = *logger;
  }
  (*notification.params)["data"] = std::move(data);

  sendNotification(context, std::move(notification));
  return true;
}

auto Server::logLevel() const -> LogLevel
{
  const std::scoped_lock lock(utilityMutex_);
  return logLevel_;
}

auto Server::paginateList(ListEndpoint endpoint, const std::optional<std::string> &cursor, std::size_t totalItems, std::size_t pageSize) -> PaginationWindow
{
  if (pageSize == 0)
  {
    throw std::invalid_argument("Pagination page size must be greater than zero");
  }

  std::size_t startIndex = 0;
  if (cursor.has_value())
  {
    const auto parsedCursor = detail::parsePaginationCursor(endpoint, *cursor);
    if (!parsedCursor.has_value() || *parsedCursor > totalItems)
    {
      throw std::invalid_argument("Invalid pagination cursor");
    }

    startIndex = *parsedCursor;
  }

  const std::size_t endIndex = std::min(startIndex + pageSize, totalItems);

  PaginationWindow window;
  window.startIndex = startIndex;
  window.endIndex = endIndex;
  if (endIndex < totalItems)
  {
    window.nextCursor = detail::makePaginationCursor(endpoint, endIndex);
  }

  return window;
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

  router_.registerRequestHandler(std::string(detail::kLoggingSetLevelMethod),
                                 [this](const jsonrpc::RequestContext &, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleLoggingSetLevelRequest(request)); });

  router_.registerRequestHandler(std::string(detail::kCompletionCompleteMethod),
                                 [this](const jsonrpc::RequestContext &, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleCompletionCompleteRequest(request)); });

  router_.registerRequestHandler(std::string(detail::kToolsListMethod),
                                 [this](const jsonrpc::RequestContext &, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleToolsListRequest(request)); });

  router_.registerRequestHandler(std::string(detail::kToolsCallMethod),
                                 [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleToolsCallRequest(context, request)); });

  router_.registerRequestHandler(std::string(detail::kResourcesListMethod),
                                 [this](const jsonrpc::RequestContext &, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleResourcesListRequest(request)); });

  router_.registerRequestHandler(std::string(detail::kResourcesReadMethod),
                                 [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleResourcesReadRequest(context, request)); });

  router_.registerRequestHandler(std::string(detail::kResourcesTemplatesListMethod),
                                 [this](const jsonrpc::RequestContext &, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleResourceTemplatesListRequest(request)); });

  router_.registerRequestHandler(std::string(detail::kResourcesSubscribeMethod),
                                 [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleResourcesSubscribeRequest(context, request)); });

  router_.registerRequestHandler(std::string(detail::kResourcesUnsubscribeMethod),
                                 [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleResourcesUnsubscribeRequest(context, request)); });

  router_.registerRequestHandler(std::string(detail::kPromptsListMethod),
                                 [this](const jsonrpc::RequestContext &, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handlePromptsListRequest(request)); });

  router_.registerRequestHandler(std::string(detail::kPromptsGetMethod),
                                 [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handlePromptsGetRequest(context, request)); });

  router_.registerRequestHandler(std::string(detail::kTasksGetMethod),
                                 [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleTasksGetRequest(context, request)); });

  router_.registerRequestHandler(std::string(detail::kTasksResultMethod),
                                 [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleTasksResultRequest(context, request)); });

  router_.registerRequestHandler(std::string(detail::kTasksListMethod),
                                 [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleTasksListRequest(context, request)); });

  router_.registerRequestHandler(std::string(detail::kTasksCancelMethod),
                                 [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleTasksCancelRequest(context, request)); });
}

auto Server::handleToolsListRequest(const jsonrpc::Request &request) -> jsonrpc::Response  // NOLINT(readability-function-cognitive-complexity)
{
  std::optional<std::string> cursor;
  if (request.params.has_value())
  {
    if (!request.params->is_object())
    {
      return detail::makeInvalidParamsResponse(request.id, "tools/list requires params to be an object when provided");
    }

    if (request.params->contains("cursor"))
    {
      if (!(*request.params)["cursor"].is_string())
      {
        return detail::makeInvalidParamsResponse(request.id, "tools/list requires params.cursor to be a string");
      }

      cursor = (*request.params)["cursor"].as<std::string>();
    }
  }

  std::vector<ToolDefinition> toolDefinitions;
  {
    const std::scoped_lock lock(toolsMutex_);
    toolDefinitions.reserve(tools_.size());
    for (const auto &registeredTool : tools_)
    {
      toolDefinitions.push_back(registeredTool.definition);
    }
  }

  PaginationWindow window;
  try
  {
    window = paginateList(ListEndpoint::kTools, cursor, toolDefinitions.size(), detail::kToolsPageSize);
  }
  catch (const std::invalid_argument &)
  {
    return detail::makeInvalidParamsResponse(request.id, "Invalid tools/list cursor");
  }

  jsonrpc::JsonValue toolsJson = jsonrpc::JsonValue::array();
  for (std::size_t index = window.startIndex; index < window.endIndex; ++index)
  {
    const ToolDefinition &definition = toolDefinitions[index];
    jsonrpc::JsonValue toolJson = jsonrpc::JsonValue::object();
    toolJson["name"] = definition.name;
    if (definition.title.has_value())
    {
      toolJson["title"] = *definition.title;
    }
    if (definition.description.has_value())
    {
      toolJson["description"] = *definition.description;
    }
    if (definition.icons.has_value())
    {
      toolJson["icons"] = *definition.icons;
    }
    toolJson["inputSchema"] = definition.inputSchema;
    if (definition.outputSchema.has_value())
    {
      toolJson["outputSchema"] = *definition.outputSchema;
    }
    if (definition.annotations.has_value())
    {
      toolJson["annotations"] = *definition.annotations;
    }
    if (definition.execution.has_value())
    {
      toolJson["execution"] = *definition.execution;
    }
    if (definition.metadata.has_value())
    {
      toolJson["_meta"] = *definition.metadata;
    }

    toolsJson.push_back(std::move(toolJson));
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = jsonrpc::JsonValue::object();
  response.result["tools"] = std::move(toolsJson);
  if (window.nextCursor.has_value())
  {
    response.result["nextCursor"] = *window.nextCursor;
  }

  return response;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto Server::handleToolsCallRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request)
  -> jsonrpc::Response  // NOLINT(readability-function-cognitive-complexity)
{
  if (!request.params.has_value() || !request.params->is_object())
  {
    return detail::makeInvalidParamsResponse(request.id, "tools/call requires params object");
  }

  const jsonrpc::JsonValue &params = *request.params;
  if (!params.contains("name") || !params["name"].is_string())
  {
    return detail::makeInvalidParamsResponse(request.id, "tools/call requires string params.name");
  }

  const std::string toolName = params["name"].as<std::string>();
  jsonrpc::JsonValue arguments = jsonrpc::JsonValue::object();
  if (params.contains("arguments"))
  {
    if (!params["arguments"].is_object())
    {
      return detail::makeInvalidParamsResponse(request.id, "tools/call requires params.arguments to be an object when provided");
    }

    arguments = params["arguments"];
  }

  std::optional<ToolDefinition> definition;
  ToolHandler handler;
  {
    const std::scoped_lock lock(toolsMutex_);
    const auto toolIter =
      std::find_if(tools_.begin(), tools_.end(), [&toolName](const RegisteredTool &registeredTool) -> bool { return registeredTool.definition.name == toolName; });
    if (toolIter == tools_.end())
    {
      return detail::makeInvalidParamsResponse(request.id, "Unknown tool: " + toolName);
    }

    definition = toolIter->definition;
    handler = toolIter->handler;
  }

  if (!definition.has_value() || handler == nullptr)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "Tool registration is incomplete"), request.id);
  }

  const bool taskCapabilityEnabled = configuration_.capabilities.tasks().has_value() && configuration_.capabilities.tasks()->toolsCall;
  if (!taskCapabilityEnabled)
  {
    return detail::executeToolCall(context, request.id, toolName, *definition, handler, std::move(arguments));
  }

  std::string taskParseError;
  const util::TaskAugmentationRequest taskAugmentation = util::parseTaskAugmentation(request.params, &taskParseError);
  if (!taskParseError.empty())
  {
    return detail::makeInvalidParamsResponse(request.id, taskParseError);
  }

  const detail::ToolTaskSupport toolTaskSupport = detail::parseToolTaskSupport(*definition);
  if (!taskAugmentation.requested && toolTaskSupport == detail::ToolTaskSupport::kRequired)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeMethodNotFoundError(std::nullopt, "Task augmentation required for this tool"), request.id);
  }

  if (taskAugmentation.requested && toolTaskSupport == detail::ToolTaskSupport::kForbidden)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeMethodNotFoundError(std::nullopt, "Task augmentation is not supported for this tool"), request.id);
  }

  if (!taskAugmentation.requested)
  {
    return detail::executeToolCall(context, request.id, toolName, *definition, handler, std::move(arguments));
  }

  if (!taskReceiver_)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "Task receiver is unavailable"), request.id);
  }

  util::CreateTaskResult createTaskResult;
  try
  {
    createTaskResult = taskReceiver_->createTask(context, taskAugmentation, std::string("The operation is now in progress."));
  }
  catch (const std::exception &error)
  {
    return detail::makeInvalidParamsResponse(request.id, std::string("Task creation rejected: ") + error.what());
  }

  std::string taskId = createTaskResult.task.taskId;
  ToolDefinition definitionCopy = *definition;
  ToolHandler handlerCopy = handler;
  jsonrpc::JsonValue argumentsCopy = std::move(arguments);
  jsonrpc::RequestContext backgroundContext = context;
  const std::shared_ptr<util::TaskReceiver> taskReceiver = taskReceiver_;
  const ErrorReporter errorReporter = configuration_.sessionOptions.errorReporter;
  std::thread(::mcp::detail::threadBoundary(
                // NOLINTNEXTLINE(bugprone-exception-escape) - All exceptions are handled inside this lambda and via threadBoundary.
                [taskId = std::move(taskId),
                 toolName,
                 definitionCopy = std::move(definitionCopy),
                 handlerCopy = std::move(handlerCopy),
                 argumentsCopy = std::move(argumentsCopy),
                 backgroundContext = std::move(backgroundContext),
                 taskReceiver,
                 errorReporter]() mutable noexcept -> void
                {
                  try
                  {
                    const jsonrpc::Response toolResponse = detail::executeToolCall(
                      backgroundContext, jsonrpc::RequestId {std::string("task-") + taskId}, toolName, definitionCopy, handlerCopy, std::move(argumentsCopy));

                    util::TaskStatus successStatus = util::TaskStatus::kCompleted;
                    if (std::holds_alternative<jsonrpc::SuccessResponse>(toolResponse))
                    {
                      const auto &success = std::get<jsonrpc::SuccessResponse>(toolResponse);
                      if (success.result.contains("isError") && success.result["isError"].is_bool() && success.result["isError"].as<bool>())
                      {
                        successStatus = util::TaskStatus::kFailed;
                      }
                    }

                    static_cast<void>(taskReceiver->completeTaskWithResponse(backgroundContext, taskId, toolResponse, successStatus));
                  }
                  catch (...)
                  {
                    reportCurrentException(errorReporter, "ServerToolTask");
                  }
                },
                errorReporter,
                "ServerToolTask"))
    .detach();

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = util::createTaskResultToJson(createTaskResult);
  return response;
}

auto Server::handleResourcesListRequest(const jsonrpc::Request &request) -> jsonrpc::Response  // NOLINT(readability-function-cognitive-complexity)
{
  std::optional<std::string> cursor;
  if (request.params.has_value())
  {
    if (!request.params->is_object())
    {
      return detail::makeInvalidParamsResponse(request.id, "resources/list requires params to be an object when provided");
    }

    if (request.params->contains("cursor"))
    {
      if (!(*request.params)["cursor"].is_string())
      {
        return detail::makeInvalidParamsResponse(request.id, "resources/list requires params.cursor to be a string");
      }

      cursor = (*request.params)["cursor"].as<std::string>();
    }
  }

  std::vector<ResourceDefinition> definitions;
  {
    const std::scoped_lock lock(resourcesMutex_);
    definitions.reserve(resources_.size());
    for (const auto &registeredResource : resources_)
    {
      definitions.push_back(registeredResource.definition);
    }
  }

  PaginationWindow window;
  try
  {
    window = paginateList(ListEndpoint::kResources, cursor, definitions.size(), detail::kResourcesPageSize);
  }
  catch (const std::invalid_argument &)
  {
    return detail::makeInvalidParamsResponse(request.id, "Invalid resources/list cursor");
  }

  jsonrpc::JsonValue resourcesJson = jsonrpc::JsonValue::array();
  for (std::size_t index = window.startIndex; index < window.endIndex; ++index)
  {
    const auto &definition = definitions[index];
    jsonrpc::JsonValue resourceJson = jsonrpc::JsonValue::object();
    resourceJson["uri"] = definition.uri;
    resourceJson["name"] = definition.name;
    if (definition.title.has_value())
    {
      resourceJson["title"] = *definition.title;
    }
    if (definition.description.has_value())
    {
      resourceJson["description"] = *definition.description;
    }
    if (definition.icons.has_value())
    {
      resourceJson["icons"] = *definition.icons;
    }
    if (definition.mimeType.has_value())
    {
      resourceJson["mimeType"] = *definition.mimeType;
    }
    if (definition.size.has_value())
    {
      resourceJson["size"] = *definition.size;
    }
    if (definition.annotations.has_value())
    {
      resourceJson["annotations"] = *definition.annotations;
    }
    if (definition.metadata.has_value())
    {
      resourceJson["_meta"] = *definition.metadata;
    }

    resourcesJson.push_back(std::move(resourceJson));
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = jsonrpc::JsonValue::object();
  response.result["resources"] = std::move(resourcesJson);
  if (window.nextCursor.has_value())
  {
    response.result["nextCursor"] = *window.nextCursor;
  }

  return response;
}

auto Server::handleResourcesReadRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response
{
  if (!request.params.has_value() || !request.params->is_object())
  {
    return detail::makeInvalidParamsResponse(request.id, "resources/read requires params object");
  }

  const auto &params = *request.params;
  if (!params.contains("uri") || !params["uri"].is_string())
  {
    return detail::makeInvalidParamsResponse(request.id, "resources/read requires string params.uri");
  }

  const std::string uri = params["uri"].as<std::string>();

  ResourceReadHandler handler;
  {
    const std::scoped_lock lock(resourcesMutex_);
    const auto resourceIter =
      std::find_if(resources_.begin(), resources_.end(), [&uri](const RegisteredResource &registeredResource) -> bool { return registeredResource.definition.uri == uri; });
    if (resourceIter == resources_.end())
    {
      return detail::makeResourceNotFoundResponse(request.id, uri);
    }

    handler = resourceIter->handler;
  }

  if (handler == nullptr)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "Resource registration is incomplete"), request.id);
  }

  std::vector<ResourceContent> contents;
  try
  {
    ResourceReadContext readContext;
    readContext.requestContext = context;
    readContext.uri = uri;
    contents = handler(readContext);
  }
  catch (const std::exception &exception)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, std::string("resources/read failed: ") + exception.what()), request.id);
  }
  catch (...)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "resources/read failed: unknown error"), request.id);
  }

  jsonrpc::JsonValue contentsJson = jsonrpc::JsonValue::array();
  for (auto &content : contents)
  {
    jsonrpc::JsonValue contentJson = jsonrpc::JsonValue::object();
    contentJson["uri"] = content.uri.empty() ? uri : content.uri;
    if (content.mimeType.has_value())
    {
      contentJson["mimeType"] = *content.mimeType;
    }
    if (content.annotations.has_value())
    {
      contentJson["annotations"] = *content.annotations;
    }
    if (content.metadata.has_value())
    {
      contentJson["_meta"] = *content.metadata;
    }

    if (content.kind == ResourceContentKind::kText)
    {
      contentJson["text"] = content.value;
    }
    else
    {
      contentJson["blob"] = content.value;
    }

    contentsJson.push_back(std::move(contentJson));
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = jsonrpc::JsonValue::object();
  response.result["contents"] = std::move(contentsJson);
  return response;
}

auto Server::handleResourceTemplatesListRequest(const jsonrpc::Request &request) -> jsonrpc::Response
{
  std::optional<std::string> cursor;
  if (request.params.has_value())
  {
    if (!request.params->is_object())
    {
      return detail::makeInvalidParamsResponse(request.id, "resources/templates/list requires params to be an object when provided");
    }

    if (request.params->contains("cursor"))
    {
      if (!(*request.params)["cursor"].is_string())
      {
        return detail::makeInvalidParamsResponse(request.id, "resources/templates/list requires params.cursor to be a string");
      }

      cursor = (*request.params)["cursor"].as<std::string>();
    }
  }

  std::vector<ResourceTemplateDefinition> templates;
  {
    const std::scoped_lock lock(resourcesMutex_);
    templates = resourceTemplates_;
  }

  PaginationWindow window;
  try
  {
    window = paginateList(ListEndpoint::kResourceTemplates, cursor, templates.size(), detail::kResourceTemplatesPageSize);
  }
  catch (const std::invalid_argument &)
  {
    return detail::makeInvalidParamsResponse(request.id, "Invalid resources/templates/list cursor");
  }

  jsonrpc::JsonValue templatesJson = jsonrpc::JsonValue::array();
  for (std::size_t index = window.startIndex; index < window.endIndex; ++index)
  {
    const auto &templateDefinition = templates[index];
    jsonrpc::JsonValue templateJson = jsonrpc::JsonValue::object();
    templateJson["uriTemplate"] = templateDefinition.uriTemplate;
    templateJson["name"] = templateDefinition.name;
    if (templateDefinition.title.has_value())
    {
      templateJson["title"] = *templateDefinition.title;
    }
    if (templateDefinition.description.has_value())
    {
      templateJson["description"] = *templateDefinition.description;
    }
    if (templateDefinition.icons.has_value())
    {
      templateJson["icons"] = *templateDefinition.icons;
    }
    if (templateDefinition.mimeType.has_value())
    {
      templateJson["mimeType"] = *templateDefinition.mimeType;
    }
    if (templateDefinition.annotations.has_value())
    {
      templateJson["annotations"] = *templateDefinition.annotations;
    }
    if (templateDefinition.metadata.has_value())
    {
      templateJson["_meta"] = *templateDefinition.metadata;
    }

    templatesJson.push_back(std::move(templateJson));
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = jsonrpc::JsonValue::object();
  response.result["resourceTemplates"] = std::move(templatesJson);
  if (window.nextCursor.has_value())
  {
    response.result["nextCursor"] = *window.nextCursor;
  }

  return response;
}

auto Server::handleResourcesSubscribeRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response
{
  if (!request.params.has_value() || !request.params->is_object())
  {
    return detail::makeInvalidParamsResponse(request.id, "resources/subscribe requires params object");
  }

  const auto &params = *request.params;
  if (!params.contains("uri") || !params["uri"].is_string())
  {
    return detail::makeInvalidParamsResponse(request.id, "resources/subscribe requires string params.uri");
  }

  const std::string uri = params["uri"].as<std::string>();
  const std::string sessionKey = detail::sessionKeyForContext(context);

  {
    const std::scoped_lock lock(resourcesMutex_);
    const auto resourceIter =
      std::find_if(resources_.begin(), resources_.end(), [&uri](const RegisteredResource &registeredResource) -> bool { return registeredResource.definition.uri == uri; });
    if (resourceIter == resources_.end())
    {
      return detail::makeResourceNotFoundResponse(request.id, uri);
    }

    const auto existingSubscription =
      std::find_if(resourceSubscriptions_.begin(),
                   resourceSubscriptions_.end(),
                   [&sessionKey, &uri](const ResourceSubscription &subscription) -> bool { return subscription.sessionKey == sessionKey && subscription.uri == uri; });
    if (existingSubscription == resourceSubscriptions_.end())
    {
      resourceSubscriptions_.push_back(ResourceSubscription {sessionKey, uri});
    }
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = jsonrpc::JsonValue::object();
  return response;
}

auto Server::handleResourcesUnsubscribeRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response
{
  if (!request.params.has_value() || !request.params->is_object())
  {
    return detail::makeInvalidParamsResponse(request.id, "resources/unsubscribe requires params object");
  }

  const auto &params = *request.params;
  if (!params.contains("uri") || !params["uri"].is_string())
  {
    return detail::makeInvalidParamsResponse(request.id, "resources/unsubscribe requires string params.uri");
  }

  const std::string uri = params["uri"].as<std::string>();
  const std::string sessionKey = detail::sessionKeyForContext(context);

  {
    const std::scoped_lock lock(resourcesMutex_);
    resourceSubscriptions_.erase(
      std::remove_if(resourceSubscriptions_.begin(),
                     resourceSubscriptions_.end(),
                     [&sessionKey, &uri](const ResourceSubscription &subscription) -> bool { return subscription.sessionKey == sessionKey && subscription.uri == uri; }),
      resourceSubscriptions_.end());
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = jsonrpc::JsonValue::object();
  return response;
}

auto Server::handlePromptsListRequest(const jsonrpc::Request &request) -> jsonrpc::Response  // NOLINT(readability-function-cognitive-complexity)
{
  std::optional<std::string> cursor;
  if (request.params.has_value())
  {
    if (!request.params->is_object())
    {
      return detail::makeInvalidParamsResponse(request.id, "prompts/list requires params to be an object when provided");
    }

    if (request.params->contains("cursor"))
    {
      if (!(*request.params)["cursor"].is_string())
      {
        return detail::makeInvalidParamsResponse(request.id, "prompts/list requires params.cursor to be a string");
      }

      cursor = (*request.params)["cursor"].as<std::string>();
    }
  }

  std::vector<PromptDefinition> promptDefinitions;
  {
    const std::scoped_lock lock(promptsMutex_);
    promptDefinitions.reserve(prompts_.size());
    for (const auto &registeredPrompt : prompts_)
    {
      promptDefinitions.push_back(registeredPrompt.definition);
    }
  }

  PaginationWindow window;
  try
  {
    window = paginateList(ListEndpoint::kPrompts, cursor, promptDefinitions.size(), detail::kPromptsPageSize);
  }
  catch (const std::invalid_argument &)
  {
    return detail::makeInvalidParamsResponse(request.id, "Invalid prompts/list cursor");
  }

  jsonrpc::JsonValue promptsJson = jsonrpc::JsonValue::array();
  for (std::size_t index = window.startIndex; index < window.endIndex; ++index)
  {
    const PromptDefinition &definition = promptDefinitions[index];
    jsonrpc::JsonValue promptJson = jsonrpc::JsonValue::object();
    promptJson["name"] = definition.name;
    if (definition.title.has_value())
    {
      promptJson["title"] = *definition.title;
    }
    if (definition.description.has_value())
    {
      promptJson["description"] = *definition.description;
    }
    if (definition.icons.has_value())
    {
      promptJson["icons"] = *definition.icons;
    }
    if (!definition.arguments.empty())
    {
      jsonrpc::JsonValue argumentsJson = jsonrpc::JsonValue::array();
      for (const auto &argument : definition.arguments)
      {
        jsonrpc::JsonValue argumentJson = jsonrpc::JsonValue::object();
        argumentJson["name"] = argument.name;
        if (argument.title.has_value())
        {
          argumentJson["title"] = *argument.title;
        }
        if (argument.description.has_value())
        {
          argumentJson["description"] = *argument.description;
        }
        if (argument.required.has_value())
        {
          argumentJson["required"] = *argument.required;
        }
        if (argument.metadata.has_value())
        {
          argumentJson["_meta"] = *argument.metadata;
        }
        argumentsJson.push_back(std::move(argumentJson));
      }

      promptJson["arguments"] = std::move(argumentsJson);
    }
    if (definition.metadata.has_value())
    {
      promptJson["_meta"] = *definition.metadata;
    }

    promptsJson.push_back(std::move(promptJson));
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = jsonrpc::JsonValue::object();
  response.result["prompts"] = std::move(promptsJson);
  if (window.nextCursor.has_value())
  {
    response.result["nextCursor"] = *window.nextCursor;
  }

  return response;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto Server::handlePromptsGetRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request)
  -> jsonrpc::Response  // NOLINT(readability-function-cognitive-complexity)
{
  if (!request.params.has_value() || !request.params->is_object())
  {
    return detail::makeInvalidParamsResponse(request.id, "prompts/get requires params object");
  }

  const jsonrpc::JsonValue &params = *request.params;
  if (!params.contains("name") || !params["name"].is_string())
  {
    return detail::makeInvalidParamsResponse(request.id, "prompts/get requires string params.name");
  }

  const std::string promptName = params["name"].as<std::string>();

  jsonrpc::JsonValue arguments = jsonrpc::JsonValue::object();
  if (params.contains("arguments"))
  {
    if (!params["arguments"].is_object())
    {
      return detail::makeInvalidParamsResponse(request.id, "prompts/get requires params.arguments to be an object when provided");
    }

    arguments = params["arguments"];
    for (const auto &entry : arguments.object_range())
    {
      if (!entry.value().is_string())
      {
        return detail::makeInvalidParamsResponse(request.id, "prompts/get requires every params.arguments value to be a string");
      }
    }
  }

  std::optional<PromptDefinition> definition;
  PromptHandler handler;
  {
    const std::scoped_lock lock(promptsMutex_);
    const auto promptIter =
      std::find_if(prompts_.begin(), prompts_.end(), [&promptName](const RegisteredPrompt &registeredPrompt) -> bool { return registeredPrompt.definition.name == promptName; });
    if (promptIter == prompts_.end())
    {
      return detail::makeInvalidParamsResponse(request.id, "Unknown prompt: " + promptName);
    }

    definition = promptIter->definition;
    handler = promptIter->handler;
  }

  if (!definition.has_value() || handler == nullptr)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "Prompt registration is incomplete"), request.id);
  }

  for (const auto &argumentEntry : arguments.object_range())
  {
    const auto knownArgument =
      std::find_if(definition->arguments.begin(),
                   definition->arguments.end(),
                   [&argumentEntry](const PromptArgumentDefinition &argumentDefinition) -> bool { return argumentDefinition.name == argumentEntry.key(); });
    if (knownArgument == definition->arguments.end())
    {
      return detail::makeInvalidParamsResponse(request.id, "prompts/get arguments contains unknown argument: " + argumentEntry.key());
    }
  }

  for (const auto &argumentDefinition : definition->arguments)
  {
    const bool isRequired = argumentDefinition.required.value_or(false);
    if (isRequired && !arguments.contains(argumentDefinition.name))
    {
      return detail::makeInvalidParamsResponse(request.id, "prompts/get is missing required argument: " + argumentDefinition.name);
    }
  }

  PromptGetResult result;
  try
  {
    PromptGetContext getContext;
    getContext.requestContext = context;
    getContext.promptName = promptName;
    getContext.arguments = std::move(arguments);
    result = handler(getContext);
  }
  catch (const std::exception &exception)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, std::string("prompts/get failed: ") + exception.what()), request.id);
  }
  catch (...)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "prompts/get failed: unknown error"), request.id);
  }

  for (const auto &message : result.messages)
  {
    if ((message.role != "user" && message.role != "assistant") || !message.content.is_object())
    {
      return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "Prompt handler returned invalid messages"), request.id);
    }
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = jsonrpc::JsonValue::object();
  if (result.description.has_value())
  {
    response.result["description"] = *result.description;
  }
  else if (definition->description.has_value())
  {
    response.result["description"] = *definition->description;
  }

  jsonrpc::JsonValue messagesJson = jsonrpc::JsonValue::array();
  for (const auto &message : result.messages)
  {
    jsonrpc::JsonValue messageJson = jsonrpc::JsonValue::object();
    messageJson["role"] = message.role;
    messageJson["content"] = message.content;
    messagesJson.push_back(std::move(messageJson));
  }
  response.result["messages"] = std::move(messagesJson);

  if (result.metadata.has_value())
  {
    response.result["_meta"] = *result.metadata;
  }

  return response;
}

auto Server::handleTasksGetRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response
{
  if (!taskReceiver_)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "Task receiver is unavailable"), request.id);
  }

  return taskReceiver_->handleTasksGetRequest(context, request);
}

auto Server::handleTasksResultRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response
{
  if (!taskReceiver_)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "Task receiver is unavailable"), request.id);
  }

  return taskReceiver_->handleTasksResultRequest(context, request);
}

auto Server::handleTasksListRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response
{
  if (!taskReceiver_)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "Task receiver is unavailable"), request.id);
  }

  return taskReceiver_->handleTasksListRequest(context, request);
}

auto Server::handleTasksCancelRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response
{
  if (!taskReceiver_)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "Task receiver is unavailable"), request.id);
  }

  return taskReceiver_->handleTasksCancelRequest(context, request);
}

auto Server::handleLoggingSetLevelRequest(const jsonrpc::Request &request) -> jsonrpc::Response
{
  if (!request.params.has_value() || !request.params->is_object())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "logging/setLevel requires params object"), request.id);
  }

  const auto &params = *request.params;
  if (!params.contains("level") || !params["level"].is_string())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "logging/setLevel requires string params.level"), request.id);
  }

  const auto parsedLevel = detail::parseLogLevel(params["level"].as<std::string>());
  if (!parsedLevel.has_value())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "Invalid log level"), request.id);
  }

  {
    const std::scoped_lock lock(utilityMutex_);
    logLevel_ = *parsedLevel;
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = jsonrpc::JsonValue::object();
  return response;
}

auto Server::handleCompletionCompleteRequest(const jsonrpc::Request &request) -> jsonrpc::Response  // NOLINT(readability-function-cognitive-complexity)
{
  if (!request.params.has_value() || !request.params->is_object())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "completion/complete requires params object"), request.id);
  }

  const auto &params = *request.params;

  if (!params.contains("ref") || !params["ref"].is_object())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "completion/complete requires object params.ref"), request.id);
  }

  if (!params.contains("argument") || !params["argument"].is_object())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "completion/complete requires object params.argument"), request.id);
  }

  CompletionRequest completionRequest;
  const auto &ref = params["ref"];
  if (!ref.contains("type") || !ref["type"].is_string())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "completion/complete requires string params.ref.type"), request.id);
  }

  const std::string referenceType = ref["type"].as<std::string>();
  if (referenceType == "ref/prompt")
  {
    if (!ref.contains("name") || !ref["name"].is_string())
    {
      return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "ref/prompt requires params.ref.name"), request.id);
    }

    completionRequest.referenceType = CompletionReferenceType::kPrompt;
    completionRequest.referenceValue = ref["name"].as<std::string>();
  }
  else if (referenceType == "ref/resource")
  {
    if (!ref.contains("uri") || !ref["uri"].is_string())
    {
      return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "ref/resource requires params.ref.uri"), request.id);
    }

    completionRequest.referenceType = CompletionReferenceType::kResource;
    completionRequest.referenceValue = ref["uri"].as<std::string>();
  }
  else
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "Unsupported completion reference type"), request.id);
  }

  const auto &argument = params["argument"];
  if (!argument.contains("name") || !argument["name"].is_string() || !argument.contains("value") || !argument["value"].is_string())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "completion/complete requires string params.argument.name and params.argument.value"),
                                      request.id);
  }

  completionRequest.argumentName = argument["name"].as<std::string>();
  completionRequest.argumentValue = argument["value"].as<std::string>();

  if (params.contains("context") && params["context"].is_object() && params["context"].contains("arguments") && params["context"]["arguments"].is_object())
  {
    completionRequest.contextArguments = params["context"]["arguments"];
  }

  CompletionHandler completionHandler;
  {
    const std::scoped_lock lock(utilityMutex_);
    completionHandler = completionHandler_;
  }

  CompletionResult completionResult;
  if (completionHandler != nullptr)
  {
    try
    {
      completionResult = completionHandler(completionRequest);
    }
    catch (const std::exception &exception)
    {
      return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, std::string("completion/complete failed: ") + exception.what()), request.id);
    }
  }

  const bool truncated = completionResult.values.size() > detail::kCompletionMaxValues;
  if (truncated)
  {
    completionResult.values.resize(detail::kCompletionMaxValues);
  }

  jsonrpc::JsonValue completionJson = jsonrpc::JsonValue::object();
  completionJson["values"] = jsonrpc::JsonValue::array();
  for (const auto &value : completionResult.values)
  {
    completionJson["values"].push_back(value);
  }

  if (completionResult.total.has_value())
  {
    completionJson["total"] = static_cast<std::uint64_t>(*completionResult.total);
  }

  if (completionResult.hasMore.has_value())
  {
    completionJson["hasMore"] = *completionResult.hasMore || truncated;
  }
  else if (truncated)
  {
    completionJson["hasMore"] = true;
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = jsonrpc::JsonValue::object();
  response.result["completion"] = std::move(completionJson);
  return response;
}

auto Server::isMethodEnabledByCapability(std::string_view method) const -> bool
{
  if (method == detail::kResourcesSubscribeMethod || method == detail::kResourcesUnsubscribeMethod)
  {
    return configuration_.capabilities.resources().has_value() && configuration_.capabilities.resources()->subscribe;
  }

  if (method == detail::kTasksListMethod)
  {
    return configuration_.capabilities.tasks().has_value() && configuration_.capabilities.tasks()->list;
  }

  if (method == detail::kTasksCancelMethod)
  {
    return configuration_.capabilities.tasks().has_value() && configuration_.capabilities.tasks()->cancel;
  }

  if (method == detail::kTasksGetMethod || method == detail::kTasksResultMethod)
  {
    return configuration_.capabilities.tasks().has_value();
  }

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

}  // namespace mcp::server
