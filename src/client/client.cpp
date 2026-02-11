#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <mcp/client/client.hpp>
#include <mcp/client/elicitation.hpp>
#include <mcp/client/roots.hpp>
#include <mcp/client/sampling.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/schema/validator.hpp>
#include <mcp/server/prompts.hpp>
#include <mcp/server/resources.hpp>
#include <mcp/server/tools.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/transport/stdio.hpp>
#include <mcp/transport/transport.hpp>
#include <mcp/version.hpp>

namespace mcp
{
static constexpr std::string_view kInitializeMethod = "initialize";
static constexpr std::string_view kPingMethod = "ping";
static constexpr std::string_view kInitializedNotificationMethod = "notifications/initialized";
static constexpr std::string_view kDefaultClientName = "mcp-cpp-client";
static constexpr std::string_view kToolsListMethod = "tools/list";
static constexpr std::string_view kToolsCallMethod = "tools/call";
static constexpr std::string_view kResourcesListMethod = "resources/list";
static constexpr std::string_view kResourcesReadMethod = "resources/read";
static constexpr std::string_view kResourcesTemplatesListMethod = "resources/templates/list";
static constexpr std::string_view kPromptsListMethod = "prompts/list";
static constexpr std::string_view kPromptsGetMethod = "prompts/get";
static constexpr std::string_view kRootsListMethod = "roots/list";
static constexpr std::string_view kSamplingCreateMessageMethod = "sampling/createMessage";
static constexpr std::string_view kElicitationCreateMethod = "elicitation/create";
static constexpr std::string_view kTasksGetMethod = "tasks/get";
static constexpr std::string_view kTasksResultMethod = "tasks/result";
static constexpr std::string_view kTasksListMethod = "tasks/list";
static constexpr std::string_view kTasksCancelMethod = "tasks/cancel";
static constexpr std::string_view kTasksStatusNotificationMethod = "notifications/tasks/status";
static constexpr std::string_view kRootsListChangedNotificationMethod = "notifications/roots/list_changed";
static constexpr std::string_view kElicitationCompleteNotificationMethod = "notifications/elicitation/complete";

static auto makeReadyResponseFuture(jsonrpc::Response response) -> std::future<jsonrpc::Response>
{
  std::promise<jsonrpc::Response> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

static auto latestSupportedVersion(const std::vector<std::string> &supportedVersions) -> std::string
{
  if (supportedVersions.empty())
  {
    return std::string(kLatestProtocolVersion);
  }

  return *std::max_element(supportedVersions.begin(), supportedVersions.end());
}

static auto extractResponseId(const jsonrpc::Response &response) -> std::optional<jsonrpc::RequestId>
{
  if (std::holds_alternative<jsonrpc::SuccessResponse>(response))
  {
    return std::get<jsonrpc::SuccessResponse>(response).id;
  }

  return std::get<jsonrpc::ErrorResponse>(response).id;
}

static auto mcpSchemaValidator() -> const schema::Validator &
{
  static const schema::Validator validator = schema::Validator::loadPinnedMcpSchema();
  return validator;
}

static auto ensureValidResultSchema(const jsonrpc::JsonValue &result, std::string_view definitionName, std::string_view method) -> void
{
  const schema::ValidationResult validationResult = mcpSchemaValidator().validateInstance(result, definitionName);
  if (!validationResult.valid)
  {
    throw std::runtime_error("Schema validation failed for '" + std::string(method) + "' result: " + schema::formatDiagnostics(validationResult));
  }
}

static auto responseToResultOrThrow(const jsonrpc::Response &response, std::string_view method) -> const jsonrpc::JsonValue &
{
  if (std::holds_alternative<jsonrpc::ErrorResponse>(response))
  {
    const auto &errorResponse = std::get<jsonrpc::ErrorResponse>(response);
    std::ostringstream stream;
    stream << "Request '" << method << "' failed with code " << errorResponse.error.code;
    if (!errorResponse.error.message.empty())
    {
      stream << ": " << errorResponse.error.message;
    }

    throw std::runtime_error(stream.str());
  }

  return std::get<jsonrpc::SuccessResponse>(response).result;
}

static auto parseCursor(const jsonrpc::JsonValue &result, std::string_view method) -> std::optional<std::string>
{
  if (!result.contains("nextCursor"))
  {
    return std::nullopt;
  }

  if (!result["nextCursor"].is_string())
  {
    throw std::runtime_error("Invalid '" + std::string(method) + "' response: nextCursor must be a string when present");
  }

  return result["nextCursor"].as<std::string>();
}

static auto parseToolDefinition(const jsonrpc::JsonValue &toolJson) -> ToolDefinition
{
  if (!toolJson.is_object() || !toolJson.contains("name") || !toolJson["name"].is_string() || !toolJson.contains("inputSchema") || !toolJson["inputSchema"].is_object())
  {
    throw std::runtime_error("Invalid tools/list response: tool definition is missing required fields");
  }

  ToolDefinition definition;
  definition.name = toolJson["name"].as<std::string>();
  definition.inputSchema = toolJson["inputSchema"];

  if (toolJson.contains("title") && toolJson["title"].is_string())
  {
    definition.title = toolJson["title"].as<std::string>();
  }

  if (toolJson.contains("description") && toolJson["description"].is_string())
  {
    definition.description = toolJson["description"].as<std::string>();
  }

  if (toolJson.contains("icons"))
  {
    definition.icons = toolJson["icons"];
  }

  if (toolJson.contains("outputSchema") && toolJson["outputSchema"].is_object())
  {
    definition.outputSchema = toolJson["outputSchema"];
  }

  if (toolJson.contains("annotations"))
  {
    definition.annotations = toolJson["annotations"];
  }

  if (toolJson.contains("execution"))
  {
    definition.execution = toolJson["execution"];
  }

  if (toolJson.contains("_meta"))
  {
    definition.metadata = toolJson["_meta"];
  }

  return definition;
}

static auto parseResourceDefinition(const jsonrpc::JsonValue &resourceJson) -> ResourceDefinition
{
  if (!resourceJson.is_object() || !resourceJson.contains("uri") || !resourceJson["uri"].is_string() || !resourceJson.contains("name") || !resourceJson["name"].is_string())
  {
    throw std::runtime_error("Invalid resources/list response: resource definition is missing required fields");
  }

  ResourceDefinition definition;
  definition.uri = resourceJson["uri"].as<std::string>();
  definition.name = resourceJson["name"].as<std::string>();

  if (resourceJson.contains("title") && resourceJson["title"].is_string())
  {
    definition.title = resourceJson["title"].as<std::string>();
  }

  if (resourceJson.contains("description") && resourceJson["description"].is_string())
  {
    definition.description = resourceJson["description"].as<std::string>();
  }

  if (resourceJson.contains("icons"))
  {
    definition.icons = resourceJson["icons"];
  }

  if (resourceJson.contains("mimeType") && resourceJson["mimeType"].is_string())
  {
    definition.mimeType = resourceJson["mimeType"].as<std::string>();
  }

  if (resourceJson.contains("size") && (resourceJson["size"].is_uint64() || resourceJson["size"].is_int64()))
  {
    definition.size = resourceJson["size"].as<std::uint64_t>();
  }

  if (resourceJson.contains("annotations"))
  {
    definition.annotations = resourceJson["annotations"];
  }

  if (resourceJson.contains("_meta"))
  {
    definition.metadata = resourceJson["_meta"];
  }

  return definition;
}

static auto parseResourceContent(const jsonrpc::JsonValue &contentJson) -> ResourceContent
{
  if (!contentJson.is_object() || !contentJson.contains("uri") || !contentJson["uri"].is_string())
  {
    throw std::runtime_error("Invalid resources/read response: content is missing required uri field");
  }

  ResourceContent content;
  content.uri = contentJson["uri"].as<std::string>();

  if (contentJson.contains("mimeType") && contentJson["mimeType"].is_string())
  {
    content.mimeType = contentJson["mimeType"].as<std::string>();
  }

  if (contentJson.contains("annotations"))
  {
    content.annotations = contentJson["annotations"];
  }

  if (contentJson.contains("_meta"))
  {
    content.metadata = contentJson["_meta"];
  }

  if (contentJson.contains("text") && contentJson["text"].is_string())
  {
    content.kind = ResourceContentKind::kText;
    content.value = contentJson["text"].as<std::string>();
    return content;
  }

  if (contentJson.contains("blob") && contentJson["blob"].is_string())
  {
    content.kind = ResourceContentKind::kBlobBase64;
    content.value = contentJson["blob"].as<std::string>();
    return content;
  }

  throw std::runtime_error("Invalid resources/read response: content must include either text or blob");
}

static auto parseResourceTemplateDefinition(const jsonrpc::JsonValue &templateJson) -> ResourceTemplateDefinition
{
  if (!templateJson.is_object() || !templateJson.contains("uriTemplate") || !templateJson["uriTemplate"].is_string() || !templateJson.contains("name")
      || !templateJson["name"].is_string())
  {
    throw std::runtime_error("Invalid resources/templates/list response: resource template is missing required fields");
  }

  ResourceTemplateDefinition definition;
  definition.uriTemplate = templateJson["uriTemplate"].as<std::string>();
  definition.name = templateJson["name"].as<std::string>();

  if (templateJson.contains("title") && templateJson["title"].is_string())
  {
    definition.title = templateJson["title"].as<std::string>();
  }

  if (templateJson.contains("description") && templateJson["description"].is_string())
  {
    definition.description = templateJson["description"].as<std::string>();
  }

  if (templateJson.contains("icons"))
  {
    definition.icons = templateJson["icons"];
  }

  if (templateJson.contains("mimeType") && templateJson["mimeType"].is_string())
  {
    definition.mimeType = templateJson["mimeType"].as<std::string>();
  }

  if (templateJson.contains("annotations"))
  {
    definition.annotations = templateJson["annotations"];
  }

  if (templateJson.contains("_meta"))
  {
    definition.metadata = templateJson["_meta"];
  }

  return definition;
}

static auto parsePromptArgument(const jsonrpc::JsonValue &argumentJson) -> PromptArgumentDefinition
{
  if (!argumentJson.is_object() || !argumentJson.contains("name") || !argumentJson["name"].is_string())
  {
    throw std::runtime_error("Invalid prompts/list response: prompt argument is missing required name field");
  }

  PromptArgumentDefinition argument;
  argument.name = argumentJson["name"].as<std::string>();

  if (argumentJson.contains("title") && argumentJson["title"].is_string())
  {
    argument.title = argumentJson["title"].as<std::string>();
  }

  if (argumentJson.contains("description") && argumentJson["description"].is_string())
  {
    argument.description = argumentJson["description"].as<std::string>();
  }

  if (argumentJson.contains("required") && argumentJson["required"].is_bool())
  {
    argument.required = argumentJson["required"].as<bool>();
  }

  if (argumentJson.contains("_meta"))
  {
    argument.metadata = argumentJson["_meta"];
  }

  return argument;
}

static auto parsePromptDefinition(const jsonrpc::JsonValue &promptJson) -> PromptDefinition
{
  if (!promptJson.is_object() || !promptJson.contains("name") || !promptJson["name"].is_string())
  {
    throw std::runtime_error("Invalid prompts/list response: prompt definition is missing required name field");
  }

  PromptDefinition definition;
  definition.name = promptJson["name"].as<std::string>();

  if (promptJson.contains("title") && promptJson["title"].is_string())
  {
    definition.title = promptJson["title"].as<std::string>();
  }

  if (promptJson.contains("description") && promptJson["description"].is_string())
  {
    definition.description = promptJson["description"].as<std::string>();
  }

  if (promptJson.contains("icons"))
  {
    definition.icons = promptJson["icons"];
  }

  if (promptJson.contains("arguments"))
  {
    if (!promptJson["arguments"].is_array())
    {
      throw std::runtime_error("Invalid prompts/list response: prompt arguments must be an array when present");
    }

    for (const auto &argumentJson : promptJson["arguments"].array_range())
    {
      definition.arguments.push_back(parsePromptArgument(argumentJson));
    }
  }

  if (promptJson.contains("_meta"))
  {
    definition.metadata = promptJson["_meta"];
  }

  return definition;
}

static auto parsePromptMessage(const jsonrpc::JsonValue &messageJson) -> PromptMessage
{
  if (!messageJson.is_object() || !messageJson.contains("role") || !messageJson["role"].is_string() || !messageJson.contains("content") || !messageJson["content"].is_object())
  {
    throw std::runtime_error("Invalid prompts/get response: message must include role and object content");
  }

  PromptMessage message;
  message.role = messageJson["role"].as<std::string>();
  message.content = messageJson["content"];
  return message;
}

static auto ensureServerCapabilityAvailable(const Client &client, std::string_view capabilityName, std::string_view method) -> void
{
  const auto negotiatedCapabilities = client.negotiatedServerCapabilities();
  if (!negotiatedCapabilities.has_value())
  {
    return;
  }

  if (negotiatedCapabilities->hasCapability(capabilityName))
  {
    return;
  }

  throw CapabilityError("Server capability '" + std::string(capabilityName) + "' is required for method '" + std::string(method) + "'");
}

static auto makeRootsUnsupportedResponse(const jsonrpc::RequestId &requestId, const std::string &reason) -> jsonrpc::Response
{
  jsonrpc::JsonValue errorData = jsonrpc::JsonValue::object();
  errorData["reason"] = reason;
  return jsonrpc::makeErrorResponse(jsonrpc::makeMethodNotFoundError(std::move(errorData), "Roots not supported"), requestId);
}

static auto makeSamplingUnsupportedResponse(const jsonrpc::RequestId &requestId, const std::string &reason) -> jsonrpc::Response
{
  jsonrpc::JsonValue errorData = jsonrpc::JsonValue::object();
  errorData["reason"] = reason;
  return jsonrpc::makeErrorResponse(jsonrpc::makeMethodNotFoundError(std::move(errorData), "Sampling not supported"), requestId);
}

static auto makeSamplingInvalidParamsResponse(const jsonrpc::RequestId &requestId, std::string message) -> jsonrpc::Response
{
  return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, std::move(message)), requestId);
}

static auto makeElicitationUnsupportedResponse(const jsonrpc::RequestId &requestId, const std::string &reason) -> jsonrpc::Response
{
  jsonrpc::JsonValue errorData = jsonrpc::JsonValue::object();
  errorData["reason"] = reason;
  return jsonrpc::makeErrorResponse(jsonrpc::makeMethodNotFoundError(std::move(errorData), "Elicitation not supported"), requestId);
}

static auto makeElicitationInvalidParamsResponse(const jsonrpc::RequestId &requestId, std::string message) -> jsonrpc::Response
{
  return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, std::move(message)), requestId);
}

static auto toActionString(ElicitationAction action) -> const char *
{
  switch (action)
  {
    case ElicitationAction::kAccept:
      return "accept";
    case ElicitationAction::kDecline:
      return "decline";
    case ElicitationAction::kCancel:
      return "cancel";
  }

  return "cancel";
}

static auto validateFormSchemaPrimitiveProperty(const jsonrpc::JsonValue &propertySchema) -> bool
{
  if (!propertySchema.is_object())
  {
    return false;
  }

  if (propertySchema.contains("properties") || propertySchema.contains("items") || propertySchema.contains("additionalProperties"))
  {
    return false;
  }

  if (!propertySchema.contains("type") || !propertySchema["type"].is_string())
  {
    return false;
  }

  const std::string type = propertySchema["type"].as<std::string>();
  return type == "string" || type == "integer" || type == "number" || type == "boolean";
}

static auto validateFormRequestedSchema(const jsonrpc::JsonValue &params) -> std::optional<std::string>
{
  if (!params.contains("requestedSchema") || !params["requestedSchema"].is_object())
  {
    return "elicitation/create form mode requires object params.requestedSchema";
  }

  const auto &requestedSchema = params["requestedSchema"];
  if (!requestedSchema.contains("type") || !requestedSchema["type"].is_string() || requestedSchema["type"].as<std::string>() != "object")
  {
    return "elicitation/create form mode requires requestedSchema.type to be 'object'";
  }

  if (!requestedSchema.contains("properties") || !requestedSchema["properties"].is_object())
  {
    return "elicitation/create form mode requires requestedSchema.properties to be an object";
  }

  for (const auto &property : requestedSchema["properties"].object_range())
  {
    if (!validateFormSchemaPrimitiveProperty(property.value()))
    {
      return "elicitation/create form mode only supports flat primitive properties";
    }
  }

  if (requestedSchema.contains("required") && !requestedSchema["required"].is_array())
  {
    return "elicitation/create form mode requires requestedSchema.required to be an array when present";
  }

  return std::nullopt;
}

static auto validateFormContentValue(const jsonrpc::JsonValue &value) -> bool
{
  return value.is_string() || value.is_int64() || value.is_uint64() || value.is_double() || value.is_bool() || value.is_null();
}

static auto validateFormResultContent(const jsonrpc::JsonValue &content) -> bool
{
  if (!content.is_object())
  {
    return false;
  }

  return std::all_of(content.object_range().begin(), content.object_range().end(), [](const auto &property) -> bool { return validateFormContentValue(property.value()); });
}

static auto containsAsciiWhitespaceOrControl(std::string_view value) -> bool
{
  return std::any_of(value.begin(),
                     value.end(),
                     [](char character) -> bool
                     {
                       const auto unsignedCharacter = static_cast<unsigned char>(character);
                       return std::isspace(unsignedCharacter) != 0 || std::iscntrl(unsignedCharacter) != 0;
                     });
}

static auto isValidPort(std::string_view portText) -> bool
{
  if (portText.empty())
  {
    return false;
  }

  std::uint32_t port = 0;
  for (const char character : portText)
  {
    const auto unsignedCharacter = static_cast<unsigned char>(character);
    if (std::isdigit(unsignedCharacter) == 0)
    {
      return false;
    }

    port = (port * 10U) + static_cast<std::uint32_t>(character - '0');
    if (port > 65535U)
    {
      return false;
    }
  }

  return true;
}

static auto isValidAbsoluteUrlForElicitation(std::string_view url) -> bool
{
  if (url.empty() || containsAsciiWhitespaceOrControl(url))
  {
    return false;
  }

  const std::size_t schemeSeparator = url.find("://");
  if (schemeSeparator == std::string_view::npos || schemeSeparator == 0)
  {
    return false;
  }

  const std::string_view scheme = url.substr(0, schemeSeparator);
  if (std::isalpha(static_cast<unsigned char>(scheme.front())) == 0)
  {
    return false;
  }

  const bool schemeValid = std::all_of(scheme.begin(),
                                       scheme.end(),
                                       [](char character) -> bool
                                       {
                                         const auto unsignedCharacter = static_cast<unsigned char>(character);
                                         return std::isalnum(unsignedCharacter) != 0 || character == '+' || character == '-' || character == '.';
                                       });
  if (!schemeValid)
  {
    return false;
  }

  const std::size_t authorityStart = schemeSeparator + 3;
  std::size_t authorityEnd = url.find_first_of("/?#", authorityStart);
  if (authorityEnd == std::string_view::npos)
  {
    authorityEnd = url.size();
  }

  const std::string_view authority = url.substr(authorityStart, authorityEnd - authorityStart);
  if (authority.empty() || authority.find('@') != std::string_view::npos)
  {
    return false;
  }

  if (authority.front() == '[')
  {
    const std::size_t closingBracket = authority.find(']');
    if (closingBracket == std::string_view::npos || closingBracket <= 1)
    {
      return false;
    }

    const std::string_view remainder = authority.substr(closingBracket + 1);
    if (remainder.empty())
    {
      return true;
    }

    if (remainder.front() != ':')
    {
      return false;
    }

    return isValidPort(remainder.substr(1));
  }

  if (authority.find('[') != std::string_view::npos || authority.find(']') != std::string_view::npos)
  {
    return false;
  }

  const std::size_t firstColon = authority.find(':');
  const std::size_t lastColon = authority.rfind(':');
  if (firstColon == std::string_view::npos)
  {
    return !authority.empty();
  }

  if (firstColon != lastColon)
  {
    return false;
  }

  const std::string_view host = authority.substr(0, firstColon);
  const std::string_view port = authority.substr(firstColon + 1);
  if (host.empty())
  {
    return false;
  }

  return isValidPort(port);
}

static auto contentType(const jsonrpc::JsonValue &contentBlock) -> std::optional<std::string>
{
  if (!contentBlock.is_object() || !contentBlock.contains("type") || !contentBlock["type"].is_string())
  {
    return std::nullopt;
  }

  return contentBlock["type"].as<std::string>();
}

static auto enumerateContentBlocks(const jsonrpc::JsonValue &content, const std::function<void(const jsonrpc::JsonValue &)> &visitor) -> bool
{
  if (content.is_object())
  {
    visitor(content);
    return true;
  }

  if (content.is_array())
  {
    for (const auto &contentBlock : content.array_range())
    {
      visitor(contentBlock);
    }

    return true;
  }

  return false;
}

static auto messageContainsOnlyToolResults(const jsonrpc::JsonValue &message) -> bool
{
  if (!message.is_object() || !message.contains("content"))
  {
    return false;
  }

  bool hasAtLeastOneToolResult = false;
  bool hasNonToolResult = false;
  const bool contentWasEnumerable = enumerateContentBlocks(message["content"],
                                                           [&hasAtLeastOneToolResult, &hasNonToolResult](const jsonrpc::JsonValue &contentBlock) -> void
                                                           {
                                                             const auto blockType = contentType(contentBlock);
                                                             if (!blockType.has_value())
                                                             {
                                                               hasNonToolResult = true;
                                                               return;
                                                             }

                                                             if (*blockType == "tool_result")
                                                             {
                                                               hasAtLeastOneToolResult = true;
                                                             }
                                                             else
                                                             {
                                                               hasNonToolResult = true;
                                                             }
                                                           });

  return contentWasEnumerable && hasAtLeastOneToolResult && !hasNonToolResult;
}

static auto collectToolUseIds(const jsonrpc::JsonValue &message) -> std::vector<std::string>
{
  std::vector<std::string> toolUseIds;
  if (!message.is_object() || !message.contains("content"))
  {
    return toolUseIds;
  }

  static_cast<void>(enumerateContentBlocks(message["content"],
                                           [&toolUseIds](const jsonrpc::JsonValue &contentBlock) -> void
                                           {
                                             const auto blockType = contentType(contentBlock);
                                             if (!blockType.has_value() || *blockType != "tool_use")
                                             {
                                               return;
                                             }

                                             if (!contentBlock.contains("id") || !contentBlock["id"].is_string())
                                             {
                                               toolUseIds.push_back(std::string {});
                                               return;
                                             }

                                             toolUseIds.push_back(contentBlock["id"].as<std::string>());
                                           }));

  return toolUseIds;
}

static auto collectToolResultIds(const jsonrpc::JsonValue &message) -> std::vector<std::string>
{
  std::vector<std::string> toolResultIds;
  if (!messageContainsOnlyToolResults(message))
  {
    return toolResultIds;
  }

  static_cast<void>(enumerateContentBlocks(message["content"],
                                           [&toolResultIds](const jsonrpc::JsonValue &contentBlock) -> void
                                           {
                                             if (!contentBlock.contains("toolUseId") || !contentBlock["toolUseId"].is_string())
                                             {
                                               toolResultIds.push_back(std::string {});
                                               return;
                                             }

                                             toolResultIds.push_back(contentBlock["toolUseId"].as<std::string>());
                                           }));

  return toolResultIds;
}

static auto hasBalancedToolUseSequence(const jsonrpc::JsonValue &messages) -> bool
{
  if (!messages.is_array())
  {
    return false;
  }

  for (std::size_t index = 0; index < messages.size(); ++index)
  {
    const auto &message = messages[index];
    if (!message.is_object() || !message.contains("role") || !message["role"].is_string())
    {
      return false;
    }

    const std::string role = message["role"].as<std::string>();
    const auto toolUseIds = collectToolUseIds(message);
    const bool hasToolUses = !toolUseIds.empty();
    const bool hasToolResults = messageContainsOnlyToolResults(message);

    if (role == "user" && hasToolResults)
    {
      if (index == 0)
      {
        return false;
      }

      const auto &previousMessage = messages[index - 1];
      if (!previousMessage.is_object() || !previousMessage.contains("role") || !previousMessage["role"].is_string() || previousMessage["role"].as<std::string>() != "assistant")
      {
        return false;
      }

      const auto previousToolUseIds = collectToolUseIds(previousMessage);
      if (previousToolUseIds.empty())
      {
        return false;
      }

      const auto toolResultIds = collectToolResultIds(message);
      if (toolResultIds.size() != previousToolUseIds.size())
      {
        return false;
      }

      std::vector<std::string> sortedToolUseIds = previousToolUseIds;
      std::vector<std::string> sortedToolResultIds = toolResultIds;
      std::sort(sortedToolUseIds.begin(), sortedToolUseIds.end());
      std::sort(sortedToolResultIds.begin(), sortedToolResultIds.end());
      if (sortedToolUseIds != sortedToolResultIds)
      {
        return false;
      }
    }

    if (role == "assistant" && hasToolUses)
    {
      if (index + 1 >= messages.size())
      {
        return false;
      }

      const auto &nextMessage = messages[index + 1];
      if (!nextMessage.is_object() || !nextMessage.contains("role") || !nextMessage["role"].is_string() || nextMessage["role"].as<std::string>() != "user")
      {
        return false;
      }

      if (!messageContainsOnlyToolResults(nextMessage))
      {
        return false;
      }
    }
  }

  return true;
}

static auto validateSamplingRequestSemantics(const Client &client, const jsonrpc::JsonValue &params) -> std::optional<std::string>
{
  if (!params.is_object())
  {
    return "sampling/createMessage requires object params";
  }

  if (!params.contains("messages") || !params["messages"].is_array())
  {
    return "sampling/createMessage requires array params.messages";
  }

  const auto negotiatedCapabilities = client.negotiatedClientCapabilities();
  const bool samplingToolsEnabled = negotiatedCapabilities.has_value() && negotiatedCapabilities->sampling().has_value() && negotiatedCapabilities->sampling()->tools;
  if ((params.contains("tools") || params.contains("toolChoice")) && !samplingToolsEnabled)
  {
    return "sampling/createMessage tools and toolChoice require negotiated sampling.tools capability";
  }

  for (const auto &message : params["messages"].array_range())
  {
    if (!message.is_object() || !message.contains("role") || !message["role"].is_string())
    {
      return "sampling/createMessage messages must include string role";
    }

    const std::string role = message["role"].as<std::string>();
    if (role != "user" && role != "assistant")
    {
      return "sampling/createMessage messages must use role 'user' or 'assistant'";
    }

    if (role == "user" && message.contains("content") && !messageContainsOnlyToolResults(message))
    {
      bool hasToolResult = false;
      static_cast<void>(enumerateContentBlocks(message["content"],
                                               [&hasToolResult](const jsonrpc::JsonValue &contentBlock) -> void
                                               {
                                                 const auto blockType = contentType(contentBlock);
                                                 if (blockType.has_value() && *blockType == "tool_result")
                                                 {
                                                   hasToolResult = true;
                                                 }
                                               }));

      if (hasToolResult)
      {
        return "sampling/createMessage user messages with tool_result must contain only tool_result blocks";
      }
    }
  }

  if (!hasBalancedToolUseSequence(params["messages"]))
  {
    return "sampling/createMessage tool_use and tool_result blocks must be balanced in sequence";
  }

  return std::nullopt;
}

static auto isFileUri(std::string_view uri) -> bool
{
  static constexpr std::string_view kFileUriPrefix = "file://";
  return uri.size() >= kFileUriPrefix.size() && uri.compare(0, kFileUriPrefix.size(), kFileUriPrefix) == 0;
}

static auto iconToJson(const Icon &icon) -> jsonrpc::JsonValue
{
  jsonrpc::JsonValue iconJson = jsonrpc::JsonValue::object();
  iconJson["src"] = icon.src();

  if (icon.mimeType().has_value())
  {
    iconJson["mimeType"] = *icon.mimeType();
  }

  if (icon.sizes().has_value())
  {
    iconJson["sizes"] = jsonrpc::JsonValue::array(icon.sizes()->begin(), icon.sizes()->end());
  }

  if (icon.theme().has_value())
  {
    iconJson["theme"] = *icon.theme();
  }

  return iconJson;
}

static auto implementationToJson(const Implementation &implementation) -> jsonrpc::JsonValue
{
  jsonrpc::JsonValue implementationJson = jsonrpc::JsonValue::object();
  implementationJson["name"] = implementation.name();
  implementationJson["version"] = implementation.version();

  if (implementation.title().has_value())
  {
    implementationJson["title"] = *implementation.title();
  }

  if (implementation.description().has_value())
  {
    implementationJson["description"] = *implementation.description();
  }

  if (implementation.websiteUrl().has_value())
  {
    implementationJson["websiteUrl"] = *implementation.websiteUrl();
  }

  if (implementation.icons().has_value())
  {
    jsonrpc::JsonValue iconsJson = jsonrpc::JsonValue::array();
    for (const auto &icon : *implementation.icons())
    {
      iconsJson.push_back(iconToJson(icon));
    }

    implementationJson["icons"] = std::move(iconsJson);
  }

  return implementationJson;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static auto clientCapabilitiesToJson(const ClientCapabilities &capabilities) -> jsonrpc::JsonValue
{
  jsonrpc::JsonValue capabilitiesJson = jsonrpc::JsonValue::object();

  if (capabilities.experimental().has_value())
  {
    capabilitiesJson["experimental"] = *capabilities.experimental();
  }

  if (capabilities.roots().has_value())
  {
    jsonrpc::JsonValue rootsJson = jsonrpc::JsonValue::object();
    rootsJson["listChanged"] = capabilities.roots()->listChanged;
    capabilitiesJson["roots"] = std::move(rootsJson);
  }

  if (capabilities.sampling().has_value())
  {
    jsonrpc::JsonValue samplingJson = jsonrpc::JsonValue::object();
    if (capabilities.sampling()->context)
    {
      samplingJson["context"] = jsonrpc::JsonValue::object();
    }

    if (capabilities.sampling()->tools)
    {
      samplingJson["tools"] = jsonrpc::JsonValue::object();
    }

    capabilitiesJson["sampling"] = std::move(samplingJson);
  }

  if (capabilities.elicitation().has_value())
  {
    jsonrpc::JsonValue elicitationJson = jsonrpc::JsonValue::object();
    if (capabilities.elicitation()->form)
    {
      elicitationJson["form"] = jsonrpc::JsonValue::object();
    }

    if (capabilities.elicitation()->url)
    {
      elicitationJson["url"] = jsonrpc::JsonValue::object();
    }

    capabilitiesJson["elicitation"] = std::move(elicitationJson);
  }

  if (capabilities.tasks().has_value())
  {
    jsonrpc::JsonValue tasksJson = jsonrpc::JsonValue::object();
    if (capabilities.tasks()->list)
    {
      tasksJson["list"] = jsonrpc::JsonValue::object();
    }

    if (capabilities.tasks()->cancel)
    {
      tasksJson["cancel"] = jsonrpc::JsonValue::object();
    }

    if (capabilities.tasks()->samplingCreateMessage || capabilities.tasks()->elicitationCreate || capabilities.tasks()->toolsCall)
    {
      jsonrpc::JsonValue requestsJson = jsonrpc::JsonValue::object();
      if (capabilities.tasks()->samplingCreateMessage)
      {
        jsonrpc::JsonValue samplingJson = jsonrpc::JsonValue::object();
        samplingJson["createMessage"] = jsonrpc::JsonValue::object();
        requestsJson["sampling"] = std::move(samplingJson);
      }

      if (capabilities.tasks()->elicitationCreate)
      {
        jsonrpc::JsonValue elicitationJson = jsonrpc::JsonValue::object();
        elicitationJson["create"] = jsonrpc::JsonValue::object();
        requestsJson["elicitation"] = std::move(elicitationJson);
      }

      if (capabilities.tasks()->toolsCall)
      {
        jsonrpc::JsonValue toolsJson = jsonrpc::JsonValue::object();
        toolsJson["call"] = jsonrpc::JsonValue::object();
        requestsJson["tools"] = std::move(toolsJson);
      }

      tasksJson["requests"] = std::move(requestsJson);
    }

    capabilitiesJson["tasks"] = std::move(tasksJson);
  }

  return capabilitiesJson;
}

class StreamableHttpClientTransport final : public transport::Transport
{
public:
  StreamableHttpClientTransport(transport::http::StreamableHttpClientOptions options,
                                transport::http::StreamableHttpClient::RequestExecutor requestExecutor,
                                std::function<void(const jsonrpc::Message &)> inboundMessageHandler)
    : client_(std::move(options), std::move(requestExecutor))
    , inboundMessageHandler_(std::move(inboundMessageHandler))
  {
  }

  auto attach(std::weak_ptr<Session> session) -> void override { static_cast<void>(session); }

  auto start() -> void override
  {
    const std::scoped_lock lock(mutex_);
    running_ = true;
  }

  auto stop() -> void override
  {
    const std::scoped_lock lock(mutex_);
    running_ = false;
  }

  auto isRunning() const noexcept -> bool override
  {
    const std::scoped_lock lock(mutex_);
    return running_;
  }

  auto send(jsonrpc::Message message) -> void override
  {
    std::function<void(const jsonrpc::Message &)> inboundMessageHandler;
    {
      const std::scoped_lock lock(mutex_);

      if (!running_)
      {
        throw std::runtime_error("HTTP transport must be running before send().");
      }

      inboundMessageHandler = inboundMessageHandler_;
    }

    const auto sendResult = client_.send(message);
    if (inboundMessageHandler == nullptr)
    {
      return;
    }

    for (const auto &inboundMessage : sendResult.messages)
    {
      inboundMessageHandler(inboundMessage);
    }

    if (sendResult.response.has_value())
    {
      std::visit([&inboundMessageHandler](const auto &typedResponse) -> void { inboundMessageHandler(jsonrpc::Message {typedResponse}); }, *sendResult.response);
    }
  }

private:
  mutable std::mutex mutex_;
  bool running_ = false;
  transport::http::StreamableHttpClient client_;
  std::function<void(const jsonrpc::Message &)> inboundMessageHandler_;
};

class SubprocessStdioClientTransport final : public transport::Transport
{
public:
  SubprocessStdioClientTransport(transport::StdioClientOptions options, std::function<void(const jsonrpc::Message &)> inboundMessageHandler)
    : options_(std::move(options))
    , inboundMessageHandler_(std::move(inboundMessageHandler))
  {
    if (options_.executablePath.empty())
    {
      throw std::invalid_argument("StdioClientOptions.executablePath must not be empty");
    }
  }

  ~SubprocessStdioClientTransport() override { stop(); }

  SubprocessStdioClientTransport(const SubprocessStdioClientTransport &) = delete;
  auto operator=(const SubprocessStdioClientTransport &) -> SubprocessStdioClientTransport & = delete;
  SubprocessStdioClientTransport(SubprocessStdioClientTransport &&) = delete;
  auto operator=(SubprocessStdioClientTransport &&) -> SubprocessStdioClientTransport & = delete;

  auto attach(std::weak_ptr<Session> session) -> void override
  {
    const std::scoped_lock lock(mutex_);
    attachedSession_ = std::move(session);
  }

  auto start() -> void override
  {
    const std::scoped_lock lock(mutex_);
    if (running_.load())
    {
      return;
    }

    transport::StdioSubprocessSpawnOptions spawnOptions;
    spawnOptions.argv.push_back(options_.executablePath);
    spawnOptions.argv.insert(spawnOptions.argv.end(), options_.arguments.begin(), options_.arguments.end());
    spawnOptions.envOverrides = options_.environment;
    spawnOptions.stderrMode = transport::StdioClientStderrMode::kCapture;

    subprocess_ = transport::StdioTransport::spawnSubprocess(spawnOptions);
    if (!subprocess_.valid())
    {
      throw std::runtime_error("Failed to spawn stdio subprocess transport");
    }

    running_.store(true);
    readerThread_ = std::thread([this]() -> void { readerLoop(); });
  }

  auto stop() -> void override
  {
    bool shouldJoin = false;
    {
      const std::scoped_lock lock(mutex_);
      running_.store(false);
      shouldJoin = readerThread_.joinable();
    }

    if (subprocess_.valid())
    {
      static_cast<void>(subprocess_.shutdown());
    }

    if (shouldJoin && readerThread_.joinable())
    {
      readerThread_.join();
    }
  }

  auto isRunning() const noexcept -> bool override { return running_.load(); }

  auto send(jsonrpc::Message message) -> void override
  {
    const std::scoped_lock lock(mutex_);
    if (!running_.load() || !subprocess_.valid())
    {
      throw std::runtime_error("Stdio subprocess transport must be running before send().");
    }

    jsonrpc::EncodeOptions encodeOptions;
    encodeOptions.disallowEmbeddedNewlines = true;
    subprocess_.writeLine(jsonrpc::serializeMessage(message, encodeOptions));
  }

private:
  auto readerLoop() -> void
  {
    while (true)
    {
      std::string line;
      if (!running_.load() || !subprocess_.valid())
      {
        break;
      }

      bool hasLine = false;
      try
      {
        hasLine = subprocess_.readLine(line);
      }
      catch (const std::exception &error)
      {
        static_cast<void>(error);
        running_.store(false);
        break;
      }

      if (!hasLine)
      {
        running_.store(false);
        break;
      }

      if (line.empty())
      {
        continue;
      }

      try
      {
        const jsonrpc::Message inboundMessage = jsonrpc::parseMessage(line);
        if (inboundMessageHandler_)
        {
          inboundMessageHandler_(inboundMessage);
        }
      }
      catch (const std::exception &error)
      {
        static_cast<void>(error);
      }
    }
  }

  mutable std::mutex mutex_;
  std::atomic<bool> running_ {false};
  transport::StdioClientOptions options_;
  std::weak_ptr<Session> attachedSession_;
  transport::StdioSubprocess subprocess_;
  std::thread readerThread_;
  std::function<void(const jsonrpc::Message &)> inboundMessageHandler_;
};

auto Client::create(SessionOptions options) -> std::shared_ptr<Client>
{
  return std::make_shared<Client>(std::make_shared<Session>(std::move(options)));
}

Client::Client(std::shared_ptr<Session> session)
  : session_(std::move(session))
{
  if (!session_)
  {
    throw std::invalid_argument("Client requires a non-null session");
  }

  session_->setRole(SessionRole::kClient);

  router_.setOutboundMessageSender([this](const jsonrpc::RequestContext &, jsonrpc::Message message) -> void { dispatchOutboundMessage(std::move(message)); });

  taskReceiver_ = std::make_shared<util::TaskReceiver>(std::make_shared<util::InMemoryTaskStore>());
  taskReceiver_->setStatusObserver(
    [this](const jsonrpc::RequestContext &context, const util::Task &task) -> void
    {
      const auto negotiatedCapabilities = negotiatedClientCapabilities();
      if (!negotiatedCapabilities.has_value() || !negotiatedCapabilities->tasks().has_value())
      {
        return;
      }

      if (!session_->canSendNotification(kTasksStatusNotificationMethod))
      {
        return;
      }

      jsonrpc::Notification notification;
      notification.method = std::string(kTasksStatusNotificationMethod);
      notification.params = util::taskToJson(task);
      router_.sendNotification(context, std::move(notification));
    });

  router_.registerRequestHandler(std::string(kPingMethod),
                                 [](const jsonrpc::RequestContext &, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 {
                                   jsonrpc::SuccessResponse response;
                                   response.id = request.id;
                                   response.result = jsonrpc::JsonValue::object();
                                   return makeReadyResponseFuture(jsonrpc::Response {std::move(response)});
                                 });

  router_.registerRequestHandler(
    std::string(kRootsListMethod),
    [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
    {
      const auto negotiatedCapabilities = negotiatedClientCapabilities();
      if (!negotiatedCapabilities.has_value() || !negotiatedCapabilities->roots().has_value())
      {
        return makeReadyResponseFuture(makeRootsUnsupportedResponse(request.id, "Client does not have roots capability"));
      }

      std::optional<RootsProvider> rootsProvider;
      {
        const std::scoped_lock lock(mutex_);
        rootsProvider = rootsProvider_;
      }

      if (!rootsProvider.has_value())
      {
        return makeReadyResponseFuture(makeRootsUnsupportedResponse(request.id, "Client does not have a registered roots provider"));
      }

      std::vector<RootEntry> roots;
      try
      {
        roots = (*rootsProvider)(RootsListContext {context});
      }
      catch (const std::exception &error)
      {
        return makeReadyResponseFuture(jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, std::string("roots/list failed: ") + error.what()), request.id));
      }

      jsonrpc::JsonValue rootsJson = jsonrpc::JsonValue::array();
      for (const auto &root : roots)
      {
        if (!isFileUri(root.uri))
        {
          return makeReadyResponseFuture(
            jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "roots/list provider returned invalid root URI; expected file://"), request.id));
        }

        jsonrpc::JsonValue rootJson = jsonrpc::JsonValue::object();
        rootJson["uri"] = root.uri;
        if (root.name.has_value())
        {
          rootJson["name"] = *root.name;
        }
        if (root.metadata.has_value())
        {
          rootJson["_meta"] = *root.metadata;
        }

        rootsJson.push_back(std::move(rootJson));
      }

      jsonrpc::SuccessResponse response;
      response.id = request.id;
      response.result = jsonrpc::JsonValue::object();
      response.result["roots"] = std::move(rootsJson);
      return makeReadyResponseFuture(jsonrpc::Response {std::move(response)});
    });

  router_.registerRequestHandler(
    std::string(kSamplingCreateMessageMethod),
    [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
    {
      const auto negotiatedCapabilities = negotiatedClientCapabilities();
      if (!negotiatedCapabilities.has_value() || !negotiatedCapabilities->sampling().has_value())
      {
        return makeReadyResponseFuture(makeSamplingUnsupportedResponse(request.id, "Client does not have sampling capability"));
      }

      std::optional<SamplingCreateMessageHandler> samplingCreateMessageHandler;
      {
        const std::scoped_lock lock(mutex_);
        samplingCreateMessageHandler = samplingCreateMessageHandler_;
      }

      if (!samplingCreateMessageHandler.has_value())
      {
        return makeReadyResponseFuture(makeSamplingUnsupportedResponse(request.id, "Client does not have a registered sampling/createMessage handler"));
      }

      const bool samplingTaskSupported = negotiatedCapabilities->tasks().has_value() && negotiatedCapabilities->tasks()->samplingCreateMessage;
      util::TaskAugmentationRequest taskAugmentation;
      if (samplingTaskSupported)
      {
        std::string taskParseError;
        taskAugmentation = util::parseTaskAugmentation(request.params, &taskParseError);
        if (!taskParseError.empty())
        {
          return makeReadyResponseFuture(makeSamplingInvalidParamsResponse(request.id, taskParseError));
        }
      }

      jsonrpc::JsonValue effectiveParams = request.params.has_value() ? *request.params : jsonrpc::JsonValue::object();
      if (effectiveParams.is_object() && effectiveParams.contains("task"))
      {
        effectiveParams.erase("task");
      }

      if (const auto semanticError = validateSamplingRequestSemantics(*this, effectiveParams); semanticError.has_value())
      {
        return makeReadyResponseFuture(makeSamplingInvalidParamsResponse(request.id, *semanticError));
      }

      if (samplingTaskSupported && taskAugmentation.requested)
      {
        const util::CreateTaskResult createTaskResult = taskReceiver_->createTask(context, taskAugmentation);
        const std::string taskId = createTaskResult.task.taskId;
        const SamplingCreateMessageHandler taskHandler = *samplingCreateMessageHandler;
        const jsonrpc::RequestContext taskContext = context;
        const jsonrpc::JsonValue taskParams = effectiveParams;
        const std::shared_ptr<util::TaskReceiver> taskReceiver = taskReceiver_;

        std::thread(
          [taskReceiver, taskId, taskHandler, taskContext, taskParams]() mutable -> void
          {
            jsonrpc::Response taskResponse;
            try
            {
              std::optional<jsonrpc::JsonValue> result = taskHandler(SamplingCreateMessageContext {taskContext}, taskParams);
              if (!result.has_value())
              {
                JsonRpcError rejectionError;
                rejectionError.code = -1;
                rejectionError.message = "User rejected sampling request";
                taskResponse = jsonrpc::makeErrorResponse(std::move(rejectionError), std::int64_t {0});
              }
              else
              {
                ensureValidResultSchema(*result, "CreateMessageResult", kSamplingCreateMessageMethod);
                jsonrpc::SuccessResponse success;
                success.id = std::int64_t {0};
                success.result = std::move(*result);
                taskResponse = std::move(success);
              }
            }
            catch (const std::exception &error)
            {
              taskResponse = jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, std::string("sampling/createMessage failed: ") + error.what()), std::int64_t {0});
            }

            static_cast<void>(taskReceiver->completeTaskWithResponse(taskContext, taskId, taskResponse, util::TaskStatus::kCompleted));
          })
          .detach();

        jsonrpc::SuccessResponse response;
        response.id = request.id;
        response.result = util::createTaskResultToJson(createTaskResult);
        return makeReadyResponseFuture(jsonrpc::Response {std::move(response)});
      }

      std::optional<jsonrpc::JsonValue> result;
      try
      {
        result = (*samplingCreateMessageHandler)(SamplingCreateMessageContext {context}, effectiveParams);
      }
      catch (const std::exception &error)
      {
        return makeReadyResponseFuture(
          jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, std::string("sampling/createMessage failed: ") + error.what()), request.id));
      }

      if (!result.has_value())
      {
        JsonRpcError rejectionError;
        rejectionError.code = -1;
        rejectionError.message = "User rejected sampling request";
        return makeReadyResponseFuture(jsonrpc::makeErrorResponse(std::move(rejectionError), request.id));
      }

      ensureValidResultSchema(*result, "CreateMessageResult", kSamplingCreateMessageMethod);

      jsonrpc::SuccessResponse response;
      response.id = request.id;
      response.result = std::move(*result);
      return makeReadyResponseFuture(jsonrpc::Response {std::move(response)});
    });

  router_.registerRequestHandler(
    std::string(kElicitationCreateMethod),
    [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
    {
      const auto negotiatedCapabilities = negotiatedClientCapabilities();
      if (!negotiatedCapabilities.has_value() || !negotiatedCapabilities->elicitation().has_value())
      {
        return makeReadyResponseFuture(makeElicitationUnsupportedResponse(request.id, "Client does not have elicitation capability"));
      }

      std::optional<FormElicitationHandler> formElicitationHandler;
      std::optional<UrlElicitationHandler> urlElicitationHandler;
      {
        const std::scoped_lock lock(mutex_);
        formElicitationHandler = formElicitationHandler_;
        urlElicitationHandler = urlElicitationHandler_;
      }

      const jsonrpc::JsonValue params = request.params.has_value() ? *request.params : jsonrpc::JsonValue::object();
      if (!params.is_object())
      {
        return makeReadyResponseFuture(makeElicitationInvalidParamsResponse(request.id, "elicitation/create requires object params"));
      }

      const bool elicitationTaskSupported = negotiatedCapabilities->tasks().has_value() && negotiatedCapabilities->tasks()->elicitationCreate;
      if (elicitationTaskSupported)
      {
        std::string taskParseError;
        const util::TaskAugmentationRequest taskAugmentation = util::parseTaskAugmentation(request.params, &taskParseError);
        if (!taskParseError.empty())
        {
          return makeReadyResponseFuture(makeElicitationInvalidParamsResponse(request.id, taskParseError));
        }

        if (taskAugmentation.requested)
        {
          const util::CreateTaskResult createTaskResult = taskReceiver_->createTask(context, taskAugmentation);
          const std::string taskId = createTaskResult.task.taskId;

          jsonrpc::Request internalRequest = request;
          internalRequest.id = std::string("elicitation-task-") + taskId;
          if (internalRequest.params.has_value() && internalRequest.params->is_object() && internalRequest.params->contains("task"))
          {
            internalRequest.params->erase("task");
          }

          const std::shared_ptr<util::TaskReceiver> taskReceiver = taskReceiver_;
          const jsonrpc::RequestContext taskContext = context;
          std::weak_ptr<Client> weakClient;
          try
          {
            weakClient = shared_from_this();
          }
          catch (const std::bad_weak_ptr &)
          {
            weakClient.reset();
          }

          std::thread(
            [weakClient, taskReceiver, taskContext, internalRequest = std::move(internalRequest), taskId]() mutable -> void
            {
              jsonrpc::Response taskResponse;
              if (const std::shared_ptr<Client> client = weakClient.lock())
              {
                taskResponse = client->handleRequest(taskContext, internalRequest).get();
              }
              else
              {
                taskResponse = jsonrpc::makeErrorResponse(
                  jsonrpc::makeInternalError(std::nullopt, "Task worker could not continue because the client instance is no longer available"), std::int64_t {0});
              }

              static_cast<void>(taskReceiver->completeTaskWithResponse(taskContext, taskId, taskResponse, util::TaskStatus::kCompleted));
            })
            .detach();

          jsonrpc::SuccessResponse response;
          response.id = request.id;
          response.result = util::createTaskResultToJson(createTaskResult);
          return makeReadyResponseFuture(jsonrpc::Response {std::move(response)});
        }
      }

      std::string mode = "form";
      if (params.contains("mode"))
      {
        if (!params["mode"].is_string())
        {
          return makeReadyResponseFuture(makeElicitationInvalidParamsResponse(request.id, "elicitation/create params.mode must be a string when present"));
        }

        mode = params["mode"].as<std::string>();
      }

      if (mode == "form")
      {
        if (!negotiatedCapabilities->elicitation()->form)
        {
          return makeReadyResponseFuture(makeElicitationInvalidParamsResponse(request.id, "Server requested unsupported elicitation mode 'form'"));
        }

        if (!formElicitationHandler.has_value())
        {
          return makeReadyResponseFuture(makeElicitationUnsupportedResponse(request.id, "Client does not have a registered form elicitation handler"));
        }

        if (!params.contains("message") || !params["message"].is_string())
        {
          return makeReadyResponseFuture(makeElicitationInvalidParamsResponse(request.id, "elicitation/create form mode requires string params.message"));
        }

        if (const auto schemaError = validateFormRequestedSchema(params); schemaError.has_value())
        {
          return makeReadyResponseFuture(makeElicitationInvalidParamsResponse(request.id, *schemaError));
        }

        FormElicitationRequest formRequest;
        formRequest.message = params["message"].as<std::string>();
        formRequest.requestedSchema = params["requestedSchema"];
        if (params.contains("_meta"))
        {
          formRequest.metadata = params["_meta"];
        }

        FormElicitationResult formResult;
        try
        {
          formResult = (*formElicitationHandler)(ElicitationCreateContext {context}, formRequest);
        }
        catch (const std::exception &error)
        {
          return makeReadyResponseFuture(
            jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, std::string("elicitation/create form handling failed: ") + error.what()), request.id));
        }

        jsonrpc::JsonValue result = jsonrpc::JsonValue::object();
        result["action"] = toActionString(formResult.action);

        if (formResult.action == ElicitationAction::kAccept)
        {
          if (!formResult.content.has_value() || !validateFormResultContent(*formResult.content))
          {
            return makeReadyResponseFuture(jsonrpc::makeErrorResponse(
              jsonrpc::makeInternalError(std::nullopt, "elicitation/create form handler returned invalid accept content; expected flat primitive object"), request.id));
          }

          result["content"] = *formResult.content;
        }
        else if (formResult.content.has_value())
        {
          return makeReadyResponseFuture(
            jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, "elicitation/create form handler must omit content unless action is 'accept'"), request.id));
        }

        ensureValidResultSchema(result, "ElicitResult", kElicitationCreateMethod);

        jsonrpc::SuccessResponse response;
        response.id = request.id;
        response.result = std::move(result);
        return makeReadyResponseFuture(jsonrpc::Response {std::move(response)});
      }

      if (mode == "url")
      {
        if (!negotiatedCapabilities->elicitation()->url)
        {
          return makeReadyResponseFuture(makeElicitationInvalidParamsResponse(request.id, "Server requested unsupported elicitation mode 'url'"));
        }

        if (!urlElicitationHandler.has_value())
        {
          return makeReadyResponseFuture(makeElicitationUnsupportedResponse(request.id, "Client does not have a registered URL elicitation handler"));
        }

        if (!params.contains("message") || !params["message"].is_string())
        {
          return makeReadyResponseFuture(makeElicitationInvalidParamsResponse(request.id, "elicitation/create url mode requires string params.message"));
        }

        if (!params.contains("url") || !params["url"].is_string())
        {
          return makeReadyResponseFuture(makeElicitationInvalidParamsResponse(request.id, "elicitation/create url mode requires string params.url"));
        }

        if (!params.contains("elicitationId") || !params["elicitationId"].is_string())
        {
          return makeReadyResponseFuture(makeElicitationInvalidParamsResponse(request.id, "elicitation/create url mode requires string params.elicitationId"));
        }

        const std::string url = params["url"].as<std::string>();
        if (!isValidAbsoluteUrlForElicitation(url))
        {
          return makeReadyResponseFuture(makeElicitationInvalidParamsResponse(request.id, "elicitation/create url mode requires a valid absolute URL"));
        }

        UrlElicitationRequest urlRequest;
        urlRequest.elicitationId = params["elicitationId"].as<std::string>();
        urlRequest.message = params["message"].as<std::string>();
        urlRequest.url = url;
        if (params.contains("_meta"))
        {
          urlRequest.metadata = params["_meta"];
        }

        UrlElicitationResult urlResult;
        try
        {
          urlResult = (*urlElicitationHandler)(ElicitationCreateContext {context}, urlRequest);
        }
        catch (const std::exception &error)
        {
          return makeReadyResponseFuture(
            jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, std::string("elicitation/create url handling failed: ") + error.what()), request.id));
        }

        jsonrpc::JsonValue result = jsonrpc::JsonValue::object();
        result["action"] = toActionString(urlResult.action);
        ensureValidResultSchema(result, "ElicitResult", kElicitationCreateMethod);

        if (urlResult.action == ElicitationAction::kAccept)
        {
          const std::scoped_lock lock(mutex_);
          pendingUrlElicitationIds_.insert(urlRequest.elicitationId);
        }

        jsonrpc::SuccessResponse response;
        response.id = request.id;
        response.result = std::move(result);
        return makeReadyResponseFuture(jsonrpc::Response {std::move(response)});
      }

      return makeReadyResponseFuture(makeElicitationInvalidParamsResponse(request.id, "elicitation/create mode must be 'form' or 'url'"));
    });

  router_.registerRequestHandler(
    std::string(kTasksGetMethod),
    [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
    {
      const auto negotiatedCapabilities = negotiatedClientCapabilities();
      if (!negotiatedCapabilities.has_value() || !negotiatedCapabilities->tasks().has_value())
      {
        return makeReadyResponseFuture(jsonrpc::makeErrorResponse(jsonrpc::makeMethodNotFoundError(std::nullopt, "Tasks are not supported by this client"), request.id));
      }

      return makeReadyResponseFuture(taskReceiver_->handleTasksGetRequest(context, request));
    });

  router_.registerRequestHandler(
    std::string(kTasksResultMethod),
    [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
    {
      const auto negotiatedCapabilities = negotiatedClientCapabilities();
      if (!negotiatedCapabilities.has_value() || !negotiatedCapabilities->tasks().has_value())
      {
        return makeReadyResponseFuture(jsonrpc::makeErrorResponse(jsonrpc::makeMethodNotFoundError(std::nullopt, "Tasks are not supported by this client"), request.id));
      }

      return makeReadyResponseFuture(taskReceiver_->handleTasksResultRequest(context, request));
    });

  router_.registerRequestHandler(
    std::string(kTasksListMethod),
    [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
    {
      const auto negotiatedCapabilities = negotiatedClientCapabilities();
      if (!negotiatedCapabilities.has_value() || !negotiatedCapabilities->tasks().has_value() || !negotiatedCapabilities->tasks()->list)
      {
        return makeReadyResponseFuture(jsonrpc::makeErrorResponse(jsonrpc::makeMethodNotFoundError(std::nullopt, "tasks/list is not supported by this client"), request.id));
      }

      return makeReadyResponseFuture(taskReceiver_->handleTasksListRequest(context, request));
    });

  router_.registerRequestHandler(
    std::string(kTasksCancelMethod),
    [this](const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
    {
      const auto negotiatedCapabilities = negotiatedClientCapabilities();
      if (!negotiatedCapabilities.has_value() || !negotiatedCapabilities->tasks().has_value() || !negotiatedCapabilities->tasks()->cancel)
      {
        return makeReadyResponseFuture(jsonrpc::makeErrorResponse(jsonrpc::makeMethodNotFoundError(std::nullopt, "tasks/cancel is not supported by this client"), request.id));
      }

      return makeReadyResponseFuture(taskReceiver_->handleTasksCancelRequest(context, request));
    });

  router_.registerNotificationHandler(std::string(kElicitationCompleteNotificationMethod),
                                      [this](const jsonrpc::RequestContext &context, const jsonrpc::Notification &notification) -> void
                                      {
                                        if (!notification.params.has_value() || !notification.params->is_object() || !notification.params->contains("elicitationId")
                                            || !(*notification.params)["elicitationId"].is_string())
                                        {
                                          return;
                                        }

                                        const std::string elicitationId = (*notification.params)["elicitationId"].as<std::string>();
                                        std::optional<UrlElicitationCompletionHandler> completionHandler;
                                        {
                                          const std::scoped_lock lock(mutex_);
                                          const auto pendingElicitation = pendingUrlElicitationIds_.find(elicitationId);
                                          if (pendingElicitation == pendingUrlElicitationIds_.end())
                                          {
                                            return;
                                          }

                                          pendingUrlElicitationIds_.erase(pendingElicitation);
                                          completionHandler = urlElicitationCompletionHandler_;
                                        }

                                        if (completionHandler.has_value())
                                        {
                                          (*completionHandler)(ElicitationCreateContext {context}, elicitationId);
                                        }
                                      });
}

auto Client::session() const noexcept -> const std::shared_ptr<Session> &
{
  return session_;
}

auto Client::attachTransport(std::shared_ptr<transport::Transport> transport) -> void
{
  if (!transport)
  {
    throw std::invalid_argument("Client transport must not be null");
  }

  {
    const std::scoped_lock lock(mutex_);
    transport_ = std::move(transport);
  }

  session_->attachTransport(transport_);
  transport_->attach(session_);
}

auto Client::connectStdio(const transport::StdioClientOptions &options) -> void
{
  auto transport = std::make_shared<SubprocessStdioClientTransport>(options,
                                                                    [this](const jsonrpc::Message &inboundMessage) -> void
                                                                    {
                                                                      try
                                                                      {
                                                                        handleMessage(jsonrpc::RequestContext {}, inboundMessage);
                                                                      }
                                                                      catch (const std::exception &error)
                                                                      {
                                                                        static_cast<void>(error);
                                                                      }
                                                                    });

  attachTransport(std::move(transport));
}

auto Client::connectHttp(const transport::HttpClientOptions &options) -> void
{
  auto runtime = std::make_shared<transport::HttpClientRuntime>(options);

  transport::http::StreamableHttpClientOptions streamableOptions;
  streamableOptions.endpointUrl = options.endpointUrl;
  streamableOptions.bearerToken = options.bearerToken;
  streamableOptions.tls = options.tls;
  streamableOptions.sessionState = options.sessionState;
  streamableOptions.protocolVersionState = options.protocolVersionState;

  connectHttp(std::move(streamableOptions), [runtime](const transport::http::ServerRequest &request) -> transport::http::ServerResponse { return runtime->execute(request); });
}

auto Client::connectHttp(transport::http::StreamableHttpClientOptions options, transport::http::StreamableHttpClient::RequestExecutor requestExecutor) -> void
{
  auto transport = std::make_shared<StreamableHttpClientTransport>(std::move(options),
                                                                   std::move(requestExecutor),
                                                                   [this](const jsonrpc::Message &inboundMessage) -> void
                                                                   {
                                                                     try
                                                                     {
                                                                       handleMessage(jsonrpc::RequestContext {}, inboundMessage);
                                                                     }
                                                                     catch (const std::exception &error)
                                                                     {
                                                                       static_cast<void>(error);
                                                                     }
                                                                   });

  attachTransport(std::move(transport));
}

auto Client::setInitializeConfiguration(ClientInitializeConfiguration configuration) -> void
{
  const std::scoped_lock lock(mutex_);
  initializeConfiguration_ = std::move(configuration);
}

auto Client::initializeConfiguration() const -> ClientInitializeConfiguration
{
  const std::scoped_lock lock(mutex_);
  return initializeConfiguration_;
}

auto Client::initialize(RequestOptions options) -> std::future<jsonrpc::Response>
{
  return sendRequest(std::string(kInitializeMethod), jsonrpc::JsonValue::object(), options);
}

auto Client::listTools(std::optional<std::string> cursor, RequestOptions options) -> ListToolsResult
{
  ensureServerCapabilityAvailable(*this, "tools", kToolsListMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  if (cursor.has_value())
  {
    params["cursor"] = *cursor;
  }

  const jsonrpc::Response response = sendRequest(std::string(kToolsListMethod), std::move(params), options).get();
  const jsonrpc::JsonValue &result = responseToResultOrThrow(response, kToolsListMethod);
  ensureValidResultSchema(result, "ListToolsResult", kToolsListMethod);

  if (!result.contains("tools") || !result["tools"].is_array())
  {
    throw std::runtime_error("Invalid tools/list response: tools must be an array");
  }

  ListToolsResult parsedResult;
  parsedResult.nextCursor = parseCursor(result, kToolsListMethod);
  parsedResult.tools.reserve(result["tools"].size());
  for (const auto &toolJson : result["tools"].array_range())
  {
    parsedResult.tools.push_back(parseToolDefinition(toolJson));
  }

  return parsedResult;
}

auto Client::callTool(const std::string &name, jsonrpc::JsonValue arguments, RequestOptions options) -> CallToolResult
{
  ensureServerCapabilityAvailable(*this, "tools", kToolsCallMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  params["name"] = name;
  params["arguments"] = std::move(arguments);

  const jsonrpc::Response response = sendRequest(std::string(kToolsCallMethod), std::move(params), options).get();
  const jsonrpc::JsonValue &result = responseToResultOrThrow(response, kToolsCallMethod);
  ensureValidResultSchema(result, "CallToolResult", kToolsCallMethod);

  if (!result.contains("content") || !result["content"].is_array())
  {
    throw std::runtime_error("Invalid tools/call response: content must be an array");
  }

  CallToolResult parsedResult;
  parsedResult.content = result["content"];

  if (result.contains("structuredContent"))
  {
    parsedResult.structuredContent = result["structuredContent"];
  }

  if (result.contains("isError") && result["isError"].is_bool())
  {
    parsedResult.isError = result["isError"].as<bool>();
  }

  if (result.contains("_meta"))
  {
    parsedResult.metadata = result["_meta"];
  }

  return parsedResult;
}

auto Client::listResources(std::optional<std::string> cursor, RequestOptions options) -> ListResourcesResult
{
  ensureServerCapabilityAvailable(*this, "resources", kResourcesListMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  if (cursor.has_value())
  {
    params["cursor"] = *cursor;
  }

  const jsonrpc::Response response = sendRequest(std::string(kResourcesListMethod), std::move(params), options).get();
  const jsonrpc::JsonValue &result = responseToResultOrThrow(response, kResourcesListMethod);
  ensureValidResultSchema(result, "ListResourcesResult", kResourcesListMethod);

  if (!result.contains("resources") || !result["resources"].is_array())
  {
    throw std::runtime_error("Invalid resources/list response: resources must be an array");
  }

  ListResourcesResult parsedResult;
  parsedResult.nextCursor = parseCursor(result, kResourcesListMethod);
  parsedResult.resources.reserve(result["resources"].size());
  for (const auto &resourceJson : result["resources"].array_range())
  {
    parsedResult.resources.push_back(parseResourceDefinition(resourceJson));
  }

  return parsedResult;
}

auto Client::readResource(const std::string &uri, RequestOptions options) -> ReadResourceResult
{
  ensureServerCapabilityAvailable(*this, "resources", kResourcesReadMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  params["uri"] = uri;

  const jsonrpc::Response response = sendRequest(std::string(kResourcesReadMethod), std::move(params), options).get();
  const jsonrpc::JsonValue &result = responseToResultOrThrow(response, kResourcesReadMethod);
  ensureValidResultSchema(result, "ReadResourceResult", kResourcesReadMethod);

  if (!result.contains("contents") || !result["contents"].is_array())
  {
    throw std::runtime_error("Invalid resources/read response: contents must be an array");
  }

  ReadResourceResult parsedResult;
  parsedResult.contents.reserve(result["contents"].size());
  for (const auto &contentJson : result["contents"].array_range())
  {
    parsedResult.contents.push_back(parseResourceContent(contentJson));
  }

  return parsedResult;
}

auto Client::listResourceTemplates(std::optional<std::string> cursor, RequestOptions options) -> ListResourceTemplatesResult
{
  ensureServerCapabilityAvailable(*this, "resources", kResourcesTemplatesListMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  if (cursor.has_value())
  {
    params["cursor"] = *cursor;
  }

  const jsonrpc::Response response = sendRequest(std::string(kResourcesTemplatesListMethod), std::move(params), options).get();
  const jsonrpc::JsonValue &result = responseToResultOrThrow(response, kResourcesTemplatesListMethod);
  ensureValidResultSchema(result, "ListResourceTemplatesResult", kResourcesTemplatesListMethod);

  if (!result.contains("resourceTemplates") || !result["resourceTemplates"].is_array())
  {
    throw std::runtime_error("Invalid resources/templates/list response: resourceTemplates must be an array");
  }

  ListResourceTemplatesResult parsedResult;
  parsedResult.nextCursor = parseCursor(result, kResourcesTemplatesListMethod);
  parsedResult.resourceTemplates.reserve(result["resourceTemplates"].size());
  for (const auto &templateJson : result["resourceTemplates"].array_range())
  {
    parsedResult.resourceTemplates.push_back(parseResourceTemplateDefinition(templateJson));
  }

  return parsedResult;
}

auto Client::listPrompts(std::optional<std::string> cursor, RequestOptions options) -> ListPromptsResult
{
  ensureServerCapabilityAvailable(*this, "prompts", kPromptsListMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  if (cursor.has_value())
  {
    params["cursor"] = *cursor;
  }

  const jsonrpc::Response response = sendRequest(std::string(kPromptsListMethod), std::move(params), options).get();
  const jsonrpc::JsonValue &result = responseToResultOrThrow(response, kPromptsListMethod);
  ensureValidResultSchema(result, "ListPromptsResult", kPromptsListMethod);

  if (!result.contains("prompts") || !result["prompts"].is_array())
  {
    throw std::runtime_error("Invalid prompts/list response: prompts must be an array");
  }

  ListPromptsResult parsedResult;
  parsedResult.nextCursor = parseCursor(result, kPromptsListMethod);
  parsedResult.prompts.reserve(result["prompts"].size());
  for (const auto &promptJson : result["prompts"].array_range())
  {
    parsedResult.prompts.push_back(parsePromptDefinition(promptJson));
  }

  return parsedResult;
}

auto Client::getPrompt(const std::string &name, jsonrpc::JsonValue arguments, RequestOptions options) -> PromptGetResult
{
  ensureServerCapabilityAvailable(*this, "prompts", kPromptsGetMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  params["name"] = name;
  params["arguments"] = std::move(arguments);

  const jsonrpc::Response response = sendRequest(std::string(kPromptsGetMethod), std::move(params), options).get();
  const jsonrpc::JsonValue &result = responseToResultOrThrow(response, kPromptsGetMethod);
  ensureValidResultSchema(result, "GetPromptResult", kPromptsGetMethod);

  if (!result.contains("messages") || !result["messages"].is_array())
  {
    throw std::runtime_error("Invalid prompts/get response: messages must be an array");
  }

  PromptGetResult parsedResult;

  if (result.contains("description") && result["description"].is_string())
  {
    parsedResult.description = result["description"].as<std::string>();
  }

  parsedResult.messages.reserve(result["messages"].size());
  for (const auto &messageJson : result["messages"].array_range())
  {
    parsedResult.messages.push_back(parsePromptMessage(messageJson));
  }

  if (result.contains("_meta"))
  {
    parsedResult.metadata = result["_meta"];
  }

  return parsedResult;
}

auto Client::setRootsProvider(RootsProvider provider) -> void
{
  const std::scoped_lock lock(mutex_);
  rootsProvider_ = std::move(provider);
}

auto Client::clearRootsProvider() -> void
{
  const std::scoped_lock lock(mutex_);
  rootsProvider_.reset();
}

auto Client::notifyRootsListChanged() -> bool
{
  const auto negotiatedCapabilities = negotiatedClientCapabilities();
  if (!negotiatedCapabilities.has_value() || !negotiatedCapabilities->roots().has_value() || !negotiatedCapabilities->roots()->listChanged)
  {
    return false;
  }

  if (!session_->canSendNotification(kRootsListChangedNotificationMethod))
  {
    return false;
  }

  sendNotification(std::string(kRootsListChangedNotificationMethod), jsonrpc::JsonValue::object());
  return true;
}

auto Client::setSamplingCreateMessageHandler(SamplingCreateMessageHandler handler) -> void
{
  const std::scoped_lock lock(mutex_);
  samplingCreateMessageHandler_ = std::move(handler);
}

auto Client::clearSamplingCreateMessageHandler() -> void
{
  const std::scoped_lock lock(mutex_);
  samplingCreateMessageHandler_.reset();
}

auto Client::setFormElicitationHandler(FormElicitationHandler handler) -> void
{
  const std::scoped_lock lock(mutex_);
  formElicitationHandler_ = std::move(handler);
}

auto Client::clearFormElicitationHandler() -> void
{
  const std::scoped_lock lock(mutex_);
  formElicitationHandler_.reset();
}

auto Client::setUrlElicitationHandler(UrlElicitationHandler handler) -> void
{
  const std::scoped_lock lock(mutex_);
  urlElicitationHandler_ = std::move(handler);
}

auto Client::clearUrlElicitationHandler() -> void
{
  const std::scoped_lock lock(mutex_);
  urlElicitationHandler_.reset();
}

auto Client::setUrlElicitationCompletionHandler(UrlElicitationCompletionHandler handler) -> void
{
  const std::scoped_lock lock(mutex_);
  urlElicitationCompletionHandler_ = std::move(handler);
}

auto Client::clearUrlElicitationCompletionHandler() -> void
{
  const std::scoped_lock lock(mutex_);
  urlElicitationCompletionHandler_.reset();
}

auto Client::start() -> void
{
  session_->setRole(SessionRole::kClient);
  session_->start();

  std::shared_ptr<transport::Transport> transport;
  {
    const std::scoped_lock lock(mutex_);
    transport = transport_;
  }

  if (transport && !transport->isRunning())
  {
    transport->start();
  }
}

auto Client::stop() -> void
{
  std::shared_ptr<transport::Transport> transport;
  {
    const std::scoped_lock lock(mutex_);
    transport = transport_;
  }

  if (transport && transport->isRunning())
  {
    transport->stop();
  }

  session_->stop();
}

auto Client::sendRequest(std::string method, jsonrpc::JsonValue params, RequestOptions options) -> std::future<jsonrpc::Response>
{
  jsonrpc::Request request;
  request.id = nextRequestId();
  request.method = std::move(method);
  request.params = std::move(params);

  const bool isInitializeRequest = request.method == kInitializeMethod;

  if (isInitializeRequest)
  {
    applyInitializeDefaults(request);
  }

  const jsonrpc::JsonValue lifecycleParams = request.params.has_value() ? *request.params : jsonrpc::JsonValue::object();
  static_cast<void>(session_->sendRequest(request.method, lifecycleParams, options));

  {
    const std::scoped_lock lock(mutex_);
    if (isInitializeRequest)
    {
      pendingInitializeRequestId_ = request.id;
    }
  }

  jsonrpc::OutboundRequestOptions outboundOptions;
  outboundOptions.timeout = options.timeout;
  outboundOptions.cancelOnTimeout = options.cancelOnTimeout;
  auto responseFuture = router_.sendRequest(jsonrpc::RequestContext {}, std::move(request), std::move(outboundOptions));

  if (isInitializeRequest)
  {
    const auto status = responseFuture.wait_for(std::chrono::milliseconds {0});
    if (status == std::future_status::ready)
    {
      bool initializeResponseWasAlreadyHandled = false;
      {
        const std::scoped_lock lock(mutex_);
        initializeResponseWasAlreadyHandled = !pendingInitializeRequestId_.has_value();
      }

      if (!initializeResponseWasAlreadyHandled)
      {
        jsonrpc::Response response = responseFuture.get();

        {
          const std::scoped_lock lock(mutex_);
          pendingInitializeRequestId_.reset();
        }

        try
        {
          session_->handleInitializeResponse(response);
        }
        catch (const LifecycleError &error)
        {
          static_cast<void>(error);
        }

        return makeReadyResponseFuture(std::move(response));
      }
    }
  }

  return responseFuture;
}

auto Client::sendRequestAsync(std::string method, jsonrpc::JsonValue params, const ResponseCallback &callback, RequestOptions options) -> void
{
  auto responseFuture = sendRequest(std::move(method), std::move(params), options);
  std::thread([callback, responseFuture = std::move(responseFuture)]() mutable -> void { callback(responseFuture.get()); }).detach();
}

auto Client::sendNotification(std::string method, std::optional<jsonrpc::JsonValue> params) -> void
{
  session_->sendNotification(std::move(method), std::move(params));
}

auto Client::registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void
{
  router_.registerRequestHandler(std::move(method), std::move(handler));
}

auto Client::registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void
{
  router_.registerNotificationHandler(std::move(method), std::move(handler));
}

auto Client::handleRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
{
  return router_.dispatchRequest(context, request);
}

auto Client::handleNotification(const jsonrpc::RequestContext &context, const jsonrpc::Notification &notification) -> void
{
  if (notification.method == kInitializedNotificationMethod)
  {
    session_->handleInitializedNotification();
  }

  router_.dispatchNotification(context, notification);
}

auto Client::handleResponse(const jsonrpc::RequestContext &context, const jsonrpc::Response &response) -> bool
{
  const bool initializeResponse = isPendingInitializeResponse(response);
  if (initializeResponse)
  {
    const std::scoped_lock lock(mutex_);
    pendingInitializeRequestId_.reset();
  }

  const bool dispatched = router_.dispatchResponse(context, response);

  if (initializeResponse)
  {
    session_->handleInitializeResponse(response);
  }

  return dispatched;
}

auto Client::handleMessage(const jsonrpc::RequestContext &context, const jsonrpc::Message &message) -> void
{
  if (std::holds_alternative<jsonrpc::Request>(message))
  {
    const auto &request = std::get<jsonrpc::Request>(message);
    const jsonrpc::Response response = handleRequest(context, request).get();
    std::visit([this](const auto &typedResponse) -> void { dispatchOutboundMessage(jsonrpc::Message {typedResponse}); }, response);
    return;
  }

  if (std::holds_alternative<jsonrpc::Notification>(message))
  {
    handleNotification(context, std::get<jsonrpc::Notification>(message));
    return;
  }

  if (std::holds_alternative<jsonrpc::SuccessResponse>(message))
  {
    static_cast<void>(handleResponse(context, jsonrpc::Response {std::get<jsonrpc::SuccessResponse>(message)}));
    return;
  }

  static_cast<void>(handleResponse(context, jsonrpc::Response {std::get<jsonrpc::ErrorResponse>(message)}));
}

auto Client::negotiatedProtocolVersion() const noexcept -> std::optional<std::string_view>
{
  return session_->negotiatedProtocolVersion();
}

auto Client::negotiatedParameters() const -> std::optional<NegotiatedParameters>
{
  const auto &negotiated = session_->negotiatedParameters();
  if (!negotiated.has_value())
  {
    return std::nullopt;
  }

  return negotiated;
}

auto Client::negotiatedClientCapabilities() const -> std::optional<ClientCapabilities>
{
  const auto negotiated = negotiatedParameters();
  if (!negotiated.has_value())
  {
    return std::nullopt;
  }

  return negotiated->clientCapabilities();
}

auto Client::negotiatedServerCapabilities() const -> std::optional<ServerCapabilities>
{
  const auto negotiated = negotiatedParameters();
  if (!negotiated.has_value())
  {
    return std::nullopt;
  }

  return negotiated->serverCapabilities();
}

auto Client::supportedProtocolVersions() const -> std::vector<std::string>
{
  return session_->supportedProtocolVersions();
}

auto Client::nextRequestId() -> jsonrpc::RequestId
{
  const std::scoped_lock lock(mutex_);
  return jsonrpc::RequestId {nextRequestId_++};
}

auto Client::applyInitializeDefaults(jsonrpc::Request &request) const -> void
{
  jsonrpc::JsonValue params = request.params.has_value() ? *request.params : jsonrpc::JsonValue::object();
  if (!params.is_object())
  {
    params = jsonrpc::JsonValue::object();
  }

  ClientInitializeConfiguration initializeConfiguration;
  {
    const std::scoped_lock lock(mutex_);
    initializeConfiguration = initializeConfiguration_;
  }

  if (!params.contains("protocolVersion") || !params["protocolVersion"].is_string())
  {
    if (initializeConfiguration.protocolVersion.has_value())
    {
      params["protocolVersion"] = *initializeConfiguration.protocolVersion;
    }
    else
    {
      params["protocolVersion"] = latestSupportedVersion(session_->supportedProtocolVersions());
    }
  }

  if (!params.contains("capabilities") || !params["capabilities"].is_object())
  {
    if (initializeConfiguration.capabilities.has_value())
    {
      params["capabilities"] = clientCapabilitiesToJson(*initializeConfiguration.capabilities);
    }
    else
    {
      params["capabilities"] = jsonrpc::JsonValue::object();
    }
  }

  if (!params.contains("clientInfo") || !params["clientInfo"].is_object())
  {
    if (initializeConfiguration.clientInfo.has_value())
    {
      params["clientInfo"] = implementationToJson(*initializeConfiguration.clientInfo);
    }
    else
    {
      const Implementation clientInfo {std::string(kDefaultClientName), std::string(kSdkVersion)};
      params["clientInfo"] = implementationToJson(clientInfo);
    }
  }

  request.params = std::move(params);
}

auto Client::dispatchOutboundMessage(jsonrpc::Message message) -> void
{
  std::shared_ptr<transport::Transport> transport;
  {
    const std::scoped_lock lock(mutex_);
    transport = transport_;
  }

  if (!transport)
  {
    throw std::runtime_error("Client transport is not attached");
  }

  if (!transport->isRunning())
  {
    throw std::runtime_error("Client transport must be running before sending messages");
  }

  transport->send(std::move(message));
}

auto Client::isPendingInitializeResponse(const jsonrpc::Response &response) const -> bool
{
  const auto responseId = extractResponseId(response);
  if (!responseId.has_value())
  {
    return false;
  }

  const std::scoped_lock lock(mutex_);
  return pendingInitializeRequestId_.has_value() && *pendingInitializeRequestId_ == *responseId;
}

}  // namespace mcp
