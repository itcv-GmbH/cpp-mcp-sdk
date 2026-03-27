#pragma once

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/export.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/completion_request.hpp>
#include <mcp/server/completion_result.hpp>
#include <mcp/server/list_endpoint.hpp>
#include <mcp/server/log_level.hpp>
#include <mcp/server/pagination_window.hpp>
#include <mcp/server/prompt_definition.hpp>
#include <mcp/server/prompt_handler.hpp>
#include <mcp/server/registered_prompt.hpp>
#include <mcp/server/registered_resource.hpp>
#include <mcp/server/registered_tool.hpp>
#include <mcp/server/resource_definition.hpp>
#include <mcp/server/resource_read_handler.hpp>
#include <mcp/server/resource_subscription.hpp>
#include <mcp/server/resource_template_definition.hpp>
#include <mcp/server/server_configuration.hpp>
#include <mcp/server/tool_definition.hpp>
#include <mcp/server/tool_handler.hpp>
#include <mcp/session.hpp>
#include <mcp/util/all.hpp>

namespace mcp::server
{

class MCP_SDK_EXPORT Server
{
public:
  using CompletionHandler = std::function<CompletionResult(const CompletionRequest &)>;

  static auto create(lifecycle::session::SessionOptions options = {}) -> std::shared_ptr<Server>;
  static auto create(ServerConfiguration configuration) -> std::shared_ptr<Server>;

  explicit Server(std::shared_ptr<Session> session);
  Server(std::shared_ptr<Session> session, ServerConfiguration configuration);

  auto configuration() const noexcept -> const ServerConfiguration &;

  auto session() const noexcept -> const std::shared_ptr<Session> &;

  auto start() -> void;
  auto stop() -> void;

  auto registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void;
  auto registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void;

  auto setOutboundMessageSender(jsonrpc::OutboundMessageSender sender) -> void;

  auto handleRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>;
  auto handleNotification(const jsonrpc::RequestContext &context, const jsonrpc::Notification &notification) -> void;
  auto handleResponse(const jsonrpc::RequestContext &context, const jsonrpc::Response &response) -> bool;

  auto sendRequest(const jsonrpc::RequestContext &context, jsonrpc::Request request, jsonrpc::OutboundRequestOptions options = {}) -> std::future<jsonrpc::Response>;
  auto sendNotification(const jsonrpc::RequestContext &context, jsonrpc::Notification notification) -> void;

  auto setCompletionHandler(CompletionHandler handler) -> void;
  auto registerTool(ToolDefinition definition, ToolHandler handler) -> void;
  auto unregisterTool(std::string_view name) -> bool;
  auto notifyToolsListChanged(const jsonrpc::RequestContext &context = {}) -> bool;
  auto registerResource(ResourceDefinition definition, ResourceReadHandler handler) -> void;
  auto unregisterResource(std::string_view uri) -> bool;
  auto registerResourceTemplate(ResourceTemplateDefinition definition) -> void;
  auto unregisterResourceTemplate(std::string_view uriTemplate) -> bool;
  auto registerPrompt(PromptDefinition definition, PromptHandler handler) -> void;
  auto unregisterPrompt(std::string_view name) -> bool;
  auto notifyPromptsListChanged(const jsonrpc::RequestContext &context = {}) -> bool;
  auto notifyResourceUpdated(std::string uri, const jsonrpc::RequestContext &context = {}) -> bool;
  auto notifyResourcesListChanged(const jsonrpc::RequestContext &context = {}) -> bool;
  auto emitLogMessage(const jsonrpc::RequestContext &context, LogLevel level, jsonrpc::JsonValue data, std::optional<std::string> logger = std::nullopt) -> bool;
  auto logLevel() const -> LogLevel;

  static auto paginateList(ListEndpoint endpoint, const std::optional<std::string> &cursor, std::size_t totalItems, std::size_t pageSize) -> PaginationWindow;

private:
  struct TaskStatusObserverState;

  auto configureSessionInitialization() -> void;
  auto registerCoreHandlers() -> void;
  auto handleToolsListRequest(const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleToolsCallRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleResourcesListRequest(const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleResourcesReadRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleResourceTemplatesListRequest(const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleResourcesSubscribeRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleResourcesUnsubscribeRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handlePromptsListRequest(const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handlePromptsGetRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleTasksGetRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleTasksResultRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleTasksListRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleTasksCancelRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleLoggingSetLevelRequest(const jsonrpc::Request &request) -> jsonrpc::Response;
  auto handleCompletionCompleteRequest(const jsonrpc::Request &request) -> jsonrpc::Response;

  auto isMethodEnabledByCapability(std::string_view method) const -> bool;
  static auto isCoreRequestMethod(std::string_view method) -> bool;

  std::shared_ptr<Session> session_;
  ServerConfiguration configuration_;
  jsonrpc::Router router_;
  mutable std::mutex utilityMutex_;
  mutable std::mutex toolsMutex_;
  mutable std::mutex resourcesMutex_;
  mutable std::mutex promptsMutex_;
  CompletionHandler completionHandler_;
  std::vector<RegisteredTool> tools_;
  std::vector<RegisteredResource> resources_;
  std::vector<ResourceTemplateDefinition> resourceTemplates_;
  std::vector<ResourceSubscription> resourceSubscriptions_;
  std::vector<RegisteredPrompt> prompts_;
  std::shared_ptr<TaskStatusObserverState> taskStatusObserverState_;
  std::shared_ptr<util::TaskReceiver> taskReceiver_;
  LogLevel logLevel_ = LogLevel::kDebug;
};

}  // namespace mcp::server
