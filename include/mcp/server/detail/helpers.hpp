#pragma once

#include <cstddef>
#include <cstdint>
#include <future>
#include <optional>
#include <string>
#include <string_view>

#include <mcp/export.hpp>
#include <mcp/jsonrpc/request_context.hpp>
#include <mcp/jsonrpc/response.hpp>
#include <mcp/jsonrpc/types.hpp>
#include <mcp/lifecycle/session/implementation.hpp>
#include <mcp/schema/validator.hpp>
#include <mcp/server/list_endpoint.hpp>
#include <mcp/server/log_level.hpp>
#include <mcp/server/tool_definition.hpp>
#include <mcp/server/tool_handler.hpp>

namespace mcp::server::detail
{

inline constexpr std::string_view kInitializeMethod {"initialize", sizeof("initialize") - 1};
inline constexpr std::string_view kPingMethod {"ping", sizeof("ping") - 1};
inline constexpr std::string_view kInitializedNotificationMethod {"notifications/initialized", sizeof("notifications/initialized") - 1};
inline constexpr std::string_view kMessageNotificationMethod {"notifications/message", sizeof("notifications/message") - 1};
inline constexpr std::string_view kLoggingSetLevelMethod {"logging/setLevel", sizeof("logging/setLevel") - 1};
inline constexpr std::string_view kCompletionCompleteMethod {"completion/complete", sizeof("completion/complete") - 1};
inline constexpr std::string_view kToolsListMethod {"tools/list", sizeof("tools/list") - 1};
inline constexpr std::string_view kToolsCallMethod {"tools/call", sizeof("tools/call") - 1};
inline constexpr std::string_view kToolsListChangedNotificationMethod {"notifications/tools/list_changed", sizeof("notifications/tools/list_changed") - 1};
inline constexpr std::string_view kResourcesListMethod {"resources/list", sizeof("resources/list") - 1};
inline constexpr std::string_view kResourcesReadMethod {"resources/read", sizeof("resources/read") - 1};
inline constexpr std::string_view kResourcesTemplatesListMethod {"resources/templates/list", sizeof("resources/templates/list") - 1};
inline constexpr std::string_view kResourcesSubscribeMethod {"resources/subscribe", sizeof("resources/subscribe") - 1};
inline constexpr std::string_view kResourcesUnsubscribeMethod {"resources/unsubscribe", sizeof("resources/unsubscribe") - 1};
inline constexpr std::string_view kResourcesUpdatedNotificationMethod {"notifications/resources/updated", sizeof("notifications/resources/updated") - 1};
inline constexpr std::string_view kResourcesListChangedNotificationMethod {"notifications/resources/list_changed", sizeof("notifications/resources/list_changed") - 1};
inline constexpr std::string_view kPromptsListMethod {"prompts/list", sizeof("prompts/list") - 1};
inline constexpr std::string_view kPromptsGetMethod {"prompts/get", sizeof("prompts/get") - 1};
inline constexpr std::string_view kPromptsListChangedNotificationMethod {"notifications/prompts/list_changed", sizeof("notifications/prompts/list_changed") - 1};
inline constexpr std::string_view kTasksGetMethod {"tasks/get", sizeof("tasks/get") - 1};
inline constexpr std::string_view kTasksResultMethod {"tasks/result", sizeof("tasks/result") - 1};
inline constexpr std::string_view kTasksListMethod {"tasks/list", sizeof("tasks/list") - 1};
inline constexpr std::string_view kTasksCancelMethod {"tasks/cancel", sizeof("tasks/cancel") - 1};
inline constexpr std::string_view kTasksStatusNotificationMethod {"notifications/tasks/status", sizeof("notifications/tasks/status") - 1};
inline constexpr std::string_view kDefaultServerName {"mcp-cpp-sdk", sizeof("mcp-cpp-sdk") - 1};
inline constexpr std::string_view kCursorPrefix {"mcp:v1:", sizeof("mcp:v1:") - 1};
inline constexpr std::size_t kToolsPageSize = 50;
inline constexpr std::size_t kResourcesPageSize = 50;
inline constexpr std::size_t kResourceTemplatesPageSize = 50;
inline constexpr std::size_t kPromptsPageSize = 50;
inline constexpr std::size_t kTasksPageSize = 50;
inline constexpr std::size_t kCompletionMaxValues = 100;

enum class ToolTaskSupport : std::uint8_t
{
  kForbidden,
  kOptional,
  kRequired,
};

MCP_SDK_EXPORT auto makeReadyResponseFuture(jsonrpc::Response response) -> std::future<jsonrpc::Response>;
MCP_SDK_EXPORT auto makePingResponse(const jsonrpc::RequestId &requestId) -> jsonrpc::Response;
MCP_SDK_EXPORT auto makeMethodNotFoundResponse(const jsonrpc::RequestId &requestId, std::string_view method) -> jsonrpc::Response;
MCP_SDK_EXPORT auto makeInvalidParamsResponse(const jsonrpc::RequestId &requestId, std::string message, std::optional<jsonrpc::JsonValue> data = std::nullopt) -> jsonrpc::Response;
MCP_SDK_EXPORT auto parseToolTaskSupport(const ToolDefinition &definition) -> ToolTaskSupport;
MCP_SDK_EXPORT auto makeResourceNotFoundResponse(const jsonrpc::RequestId &requestId, const std::string &uri) -> jsonrpc::Response;
MCP_SDK_EXPORT auto sessionKeyForContext(const jsonrpc::RequestContext &context) -> std::string;
MCP_SDK_EXPORT auto encodeStandardBase64(std::string_view bytes) -> std::string;
MCP_SDK_EXPORT auto executeToolCall(const jsonrpc::RequestContext &context,
                                    const jsonrpc::RequestId &requestId,
                                    const std::string &toolName,
                                    const ToolDefinition &definition,
                                    const ToolHandler &handler,
                                    jsonrpc::JsonValue arguments) -> jsonrpc::Response;
MCP_SDK_EXPORT auto mcpSchemaValidator() -> const schema::Validator &;
MCP_SDK_EXPORT auto makeLifecycleRejectedResponse(const jsonrpc::RequestId &requestId, std::string_view method) -> jsonrpc::Response;
MCP_SDK_EXPORT auto defaultServerInfo() -> lifecycle::session::Implementation;
MCP_SDK_EXPORT auto makePaginationCursor(ListEndpoint endpoint, std::size_t startIndex) -> std::string;
MCP_SDK_EXPORT auto parsePaginationCursor(ListEndpoint endpoint, std::string_view cursor) -> std::optional<std::size_t>;
MCP_SDK_EXPORT auto logLevelToString(LogLevel level) -> std::string_view;
MCP_SDK_EXPORT auto parseLogLevel(std::string_view level) -> std::optional<LogLevel>;
MCP_SDK_EXPORT auto logLevelWeight(LogLevel level) -> int;
MCP_SDK_EXPORT auto capabilityForMethod(std::string_view method) -> std::optional<std::string_view>;

}  // namespace mcp::server::detail
