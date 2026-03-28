#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mcp/server/detail/helpers.hpp"

#include <jsoncons_ext/jsonschema/common/validator.hpp>
#include <jsoncons_ext/jsonschema/evaluation_options.hpp>
#include <jsoncons_ext/jsonschema/json_schema.hpp>
#include <jsoncons_ext/jsonschema/json_schema_factory.hpp>
#include <jsoncons_ext/jsonschema/validation_message.hpp>

#include "mcp/detail/base64url.hpp"
#include "mcp/jsonrpc/error_factories.hpp"
#include "mcp/jsonrpc/request_context.hpp"
#include "mcp/jsonrpc/response.hpp"
#include "mcp/jsonrpc/response_factories.hpp"
#include "mcp/jsonrpc/success_response.hpp"
#include "mcp/jsonrpc/types.hpp"
#include "mcp/lifecycle/session/implementation.hpp"
#include "mcp/schema/validation_diagnostic.hpp"
#include "mcp/schema/validator.hpp"
#include "mcp/sdk/errors.hpp"
#include "mcp/sdk/version.hpp"
#include "mcp/server/call_tool_result.hpp"
#include "mcp/server/list_endpoint.hpp"
#include "mcp/server/log_level.hpp"
#include "mcp/server/tool_call_context.hpp"
#include "mcp/server/tool_definition.hpp"
#include "mcp/server/tool_handler.hpp"

namespace mcp::server::detail
{

constexpr std::uint32_t kBase64Shift18 = 18U;
constexpr std::uint32_t kBase64Shift16 = 16U;
constexpr std::uint32_t kBase64Shift12 = 12U;
constexpr std::uint32_t kBase64Shift8 = 8U;
constexpr std::uint32_t kBase64Shift6 = 6U;
constexpr std::uint32_t kBase64Mask6Bit = 0x3FU;
constexpr std::uint32_t kBase64Mask8Bit = 0xFFU;

constexpr std::uint32_t kBase64LowercaseOffset = 26U;
constexpr std::uint32_t kBase64DigitOffset = 52U;
constexpr std::uint32_t kBase64DashValue = 62U;
constexpr std::uint32_t kBase64UnderscoreValue = 63U;

constexpr int kLogLevelWeightDebug = 0;
constexpr int kLogLevelWeightInfo = 1;
constexpr int kLogLevelWeightNotice = 2;
constexpr int kLogLevelWeightWarning = 3;
constexpr int kLogLevelWeightError = 4;
constexpr int kLogLevelWeightCritical = 5;
constexpr int kLogLevelWeightAlert = 6;
constexpr int kLogLevelWeightEmergency = 7;

using JsonSchema = jsoncons::jsonschema::json_schema<jsonrpc::JsonValue>;
using WalkResult = jsoncons::jsonschema::walk_result;

struct SchemaValidationResult
{
  bool valid = false;
  std::vector<schema::ValidationDiagnostic> diagnostics;
};

auto validateJsonInstanceAgainstSchema(const jsonrpc::JsonValue &schemaObject, const jsonrpc::JsonValue &instance) -> SchemaValidationResult;

constexpr std::array<std::pair<std::string_view, LogLevel>, 8> kLogLevelMappings {
  std::pair<std::string_view, LogLevel> {"debug", LogLevel::kDebug},
  std::pair<std::string_view, LogLevel> {"info", LogLevel::kInfo},
  std::pair<std::string_view, LogLevel> {"notice", LogLevel::kNotice},
  std::pair<std::string_view, LogLevel> {"warning", LogLevel::kWarning},
  std::pair<std::string_view, LogLevel> {"error", LogLevel::kError},
  std::pair<std::string_view, LogLevel> {"critical", LogLevel::kCritical},
  std::pair<std::string_view, LogLevel> {"alert", LogLevel::kAlert},
  std::pair<std::string_view, LogLevel> {"emergency", LogLevel::kEmergency},
};

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

auto makeInvalidParamsResponse(const jsonrpc::RequestId &requestId, std::string message, std::optional<jsonrpc::JsonValue> data) -> jsonrpc::Response
{
  return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::move(data), std::move(message)), requestId);
}

auto parseToolTaskSupport(const ToolDefinition &definition) -> ToolTaskSupport
{
  if (!definition.execution.has_value() || !definition.execution->is_object() || !definition.execution->contains("taskSupport")
      || !(*definition.execution)["taskSupport"].is_string())
  {
    return ToolTaskSupport::kForbidden;
  }

  const std::string taskSupport = (*definition.execution)["taskSupport"].as<std::string>();
  if (taskSupport == "optional")
  {
    return ToolTaskSupport::kOptional;
  }

  if (taskSupport == "required")
  {
    return ToolTaskSupport::kRequired;
  }

  return ToolTaskSupport::kForbidden;
}

auto makeResourceNotFoundResponse(const jsonrpc::RequestId &requestId, const std::string &uri) -> jsonrpc::Response
{
  jsonrpc::JsonValue data = jsonrpc::JsonValue::object();
  data["uri"] = uri;
  return jsonrpc::makeErrorResponse(jsonrpc::makeJsonRpcError(JsonRpcErrorCode::kResourceNotFound, "Resource not found", std::move(data)), requestId);
}

auto sessionKeyForContext(const jsonrpc::RequestContext &context) -> std::string
{
  return context.sessionId.value_or(std::string {});
}

auto encodeStandardBase64(std::string_view bytes) -> std::string
{
  constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string encoded;
  encoded.reserve(((bytes.size() + 2) / 3) * 4);

  std::size_t index = 0;
  while (index + 3 <= bytes.size())
  {
    const std::uint32_t chunk = (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << kBase64Shift16)
      | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index + 1])) << kBase64Shift8) | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index + 2]));

    encoded.push_back(alphabet[(chunk >> kBase64Shift18) & kBase64Mask6Bit]);
    encoded.push_back(alphabet[(chunk >> kBase64Shift12) & kBase64Mask6Bit]);
    encoded.push_back(alphabet[(chunk >> kBase64Shift6) & kBase64Mask6Bit]);
    encoded.push_back(alphabet[chunk & kBase64Mask6Bit]);
    index += 3;
  }

  const std::size_t remaining = bytes.size() - index;
  if (remaining == 1)
  {
    const std::uint32_t chunk = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << kBase64Shift16;
    encoded.push_back(alphabet[(chunk >> kBase64Shift18) & kBase64Mask6Bit]);
    encoded.push_back(alphabet[(chunk >> kBase64Shift12) & kBase64Mask6Bit]);
    encoded.push_back('=');
    encoded.push_back('=');
  }
  else if (remaining == 2)
  {
    const std::uint32_t chunk = (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << kBase64Shift16)
      | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index + 1])) << kBase64Shift8);
    encoded.push_back(alphabet[(chunk >> kBase64Shift18) & kBase64Mask6Bit]);
    encoded.push_back(alphabet[(chunk >> kBase64Shift12) & kBase64Mask6Bit]);
    encoded.push_back(alphabet[(chunk >> kBase64Shift6) & kBase64Mask6Bit]);
    encoded.push_back('=');
  }

  return encoded;
}

auto makeTextContentBlock(std::string_view text) -> jsonrpc::JsonValue
{
  jsonrpc::JsonValue content = jsonrpc::JsonValue::array();
  jsonrpc::JsonValue textBlock = jsonrpc::JsonValue::object();
  textBlock["type"] = "text";
  textBlock["text"] = text;
  content.push_back(std::move(textBlock));
  return content;
}

auto makeSchemaValidationErrorResult(std::string_view message, const std::vector<schema::ValidationDiagnostic> &diagnostics) -> mcp::server::CallToolResult
{
  mcp::server::CallToolResult result;
  result.content = makeTextContentBlock(message);
  result.isError = true;

  if (!diagnostics.empty())
  {
    jsonrpc::JsonValue metadata = jsonrpc::JsonValue::object();
    jsonrpc::JsonValue diagnosticArray = jsonrpc::JsonValue::array();
    for (const auto &diagnostic : diagnostics)
    {
      jsonrpc::JsonValue entry = jsonrpc::JsonValue::object();
      entry["instanceLocation"] = diagnostic.instanceLocation;
      entry["evaluationPath"] = diagnostic.evaluationPath;
      entry["schemaLocation"] = diagnostic.schemaLocation;
      entry["error"] = diagnostic.message;
      diagnosticArray.push_back(std::move(entry));
    }

    metadata["validationErrors"] = std::move(diagnosticArray);
    result.metadata = std::move(metadata);
  }

  return result;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto executeToolCall(const jsonrpc::RequestContext &context,
                     const jsonrpc::RequestId &requestId,
                     const std::string &toolName,
                     const ToolDefinition &definition,
                     const ToolHandler &handler,
                     jsonrpc::JsonValue arguments) -> jsonrpc::Response
{
  static_cast<void>(context);

  const detail::SchemaValidationResult inputValidation = detail::validateJsonInstanceAgainstSchema(definition.inputSchema, arguments);
  if (!inputValidation.valid)
  {
    const CallToolResult validationError = detail::makeSchemaValidationErrorResult("Tool input validation failed for '" + toolName + "'", inputValidation.diagnostics);

    jsonrpc::SuccessResponse response;
    response.id = requestId;
    response.result = jsonrpc::JsonValue::object();
    response.result["content"] = validationError.content;
    if (validationError.metadata.has_value())
    {
      response.result["_meta"] = *validationError.metadata;
    }
    response.result["isError"] = true;
    return response;
  }

  CallToolResult result;
  try
  {
    ToolCallContext callContext;
    callContext.requestContext = context;
    callContext.toolName = toolName;
    callContext.arguments = std::move(arguments);
    result = handler(callContext);
  }
  catch (const std::exception &exception)
  {
    result.content = detail::makeTextContentBlock(std::string("Tool execution failed: ") + exception.what());
    result.isError = true;
  }
  catch (...)
  {
    result.content = detail::makeTextContentBlock("Tool execution failed: unknown error");
    result.isError = true;
  }

  if (!result.content.is_array())
  {
    result.content = detail::makeTextContentBlock("Tool returned invalid content payload");
    result.isError = true;
  }

  if (definition.outputSchema.has_value())
  {
    if (!result.structuredContent.has_value() || !result.structuredContent->is_object())
    {
      result.content = detail::makeTextContentBlock("Tool output schema requires structuredContent object");
      result.isError = true;
    }
    else
    {
      const detail::SchemaValidationResult outputValidation = detail::validateJsonInstanceAgainstSchema(*definition.outputSchema, *result.structuredContent);
      if (!outputValidation.valid)
      {
        const CallToolResult validationError = detail::makeSchemaValidationErrorResult("Tool output validation failed for '" + toolName + "'", outputValidation.diagnostics);
        result.content = validationError.content;
        result.metadata = validationError.metadata;
        result.structuredContent.reset();
        result.isError = true;
      }
    }
  }

  jsonrpc::SuccessResponse response;
  response.id = requestId;
  response.result = jsonrpc::JsonValue::object();
  response.result["content"] = result.content;
  if (result.structuredContent.has_value())
  {
    response.result["structuredContent"] = *result.structuredContent;
  }
  if (result.metadata.has_value())
  {
    response.result["_meta"] = *result.metadata;
  }
  if (result.isError)
  {
    response.result["isError"] = true;
  }

  return response;
}

auto validateJsonInstanceAgainstSchema(const jsonrpc::JsonValue &schemaObject, const jsonrpc::JsonValue &instance) -> SchemaValidationResult
{
  SchemaValidationResult result;

  if (!schemaObject.is_object())
  {
    schema::ValidationDiagnostic diagnostic;
    diagnostic.message = "Schema must be an object.";
    result.diagnostics.push_back(std::move(diagnostic));
    return result;
  }

  try
  {
    const jsoncons::jsonschema::evaluation_options options = jsoncons::jsonschema::evaluation_options {}.default_version(jsoncons::jsonschema::schema_version::draft202012());
    const JsonSchema compiledSchema = jsoncons::jsonschema::make_json_schema(jsonrpc::JsonValue(schemaObject), options);
    compiledSchema.validate(instance,
                            [&result](const jsoncons::jsonschema::validation_message &validationMessage) -> WalkResult
                            {
                              schema::ValidationDiagnostic diagnostic;
                              diagnostic.instanceLocation = validationMessage.instance_location().string();
                              diagnostic.evaluationPath = validationMessage.eval_path().string();
                              diagnostic.schemaLocation = validationMessage.schema_location().string();
                              diagnostic.message = validationMessage.message();
                              result.diagnostics.push_back(std::move(diagnostic));
                              return WalkResult::advance;
                            });
  }
  catch (const std::exception &exception)
  {
    schema::ValidationDiagnostic diagnostic;
    diagnostic.message = std::string("Schema compilation failed: ") + exception.what();
    result.diagnostics.push_back(std::move(diagnostic));
    return result;
  }

  result.valid = result.diagnostics.empty();
  return result;
}

auto mcpSchemaValidator() -> const schema::Validator &
{
  static const schema::Validator validator = schema::Validator::loadPinnedMcpSchema();
  return validator;
}

auto makeLifecycleRejectedResponse(const jsonrpc::RequestId &requestId, std::string_view method) -> jsonrpc::Response
{
  const std::string message = "Method '" + std::string(method) + "' is not valid before initialization completes.";
  return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidRequestError(std::nullopt, message), requestId);
}

auto defaultServerInfo() -> lifecycle::session::Implementation
{
  return {std::string(detail::kDefaultServerName), std::string(kSdkVersion)};
}

auto endpointToCursorLabel(ListEndpoint endpoint) -> std::string_view
{
  switch (endpoint)
  {
    case ListEndpoint::kTools:
      return "tools";
    case ListEndpoint::kResources:
      return "resources";
    case ListEndpoint::kResourceTemplates:
      return "resource_templates";
    case ListEndpoint::kPrompts:
      return "prompts";
    case ListEndpoint::kTasks:
      return "tasks";
  }

  throw std::invalid_argument("Unsupported list endpoint");
}

auto isValidCursorChar(char value) -> bool
{
  return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '-' || value == '_';
}

auto encodeCursorPayload(std::string_view payload) -> std::string
{
  return ::mcp::detail::encodeBase64UrlNoPad(payload);
}

auto decodeCursorPayload(std::string_view encoded) -> std::optional<std::string>
{
  return ::mcp::detail::decodeBase64UrlNoPad(encoded);
}

auto makePaginationCursor(ListEndpoint endpoint, std::size_t startIndex) -> std::string
{
  const std::string payload = std::string(kCursorPrefix) + std::string(endpointToCursorLabel(endpoint)) + ":" + std::to_string(startIndex);
  return encodeCursorPayload(payload);
}

auto parsePaginationCursor(ListEndpoint endpoint, std::string_view cursor) -> std::optional<std::size_t>
{
  if (cursor.empty())
  {
    return std::nullopt;
  }

  if (!std::all_of(cursor.begin(), cursor.end(), isValidCursorChar))
  {
    return std::nullopt;
  }

  const auto decoded = decodeCursorPayload(cursor);
  if (!decoded.has_value())
  {
    return std::nullopt;
  }

  const std::string expectedPrefix = std::string(kCursorPrefix) + std::string(endpointToCursorLabel(endpoint)) + ":";
  if (decoded->rfind(expectedPrefix, 0) != 0)
  {
    return std::nullopt;
  }

  const std::string indexText = decoded->substr(expectedPrefix.size());
  if (indexText.empty())
  {
    return std::nullopt;
  }

  std::size_t consumed = 0;
  std::uint64_t parsedIndex = 0;
  try
  {
    parsedIndex = std::stoull(indexText, &consumed);
  }
  catch (const std::exception &)
  {
    return std::nullopt;
  }

  if (consumed != indexText.size() || parsedIndex > std::numeric_limits<std::size_t>::max())
  {
    return std::nullopt;
  }

  return static_cast<std::size_t>(parsedIndex);
}

auto logLevelToString(LogLevel level) -> std::string_view
{
  for (const auto &[name, mappedLevel] : kLogLevelMappings)
  {
    if (mappedLevel == level)
    {
      return name;
    }
  }

  return "debug";
}

auto parseLogLevel(std::string_view level) -> std::optional<LogLevel>
{
  for (const auto &[name, mappedLevel] : kLogLevelMappings)
  {
    if (level == name)
    {
      return mappedLevel;
    }
  }

  return std::nullopt;
}

auto logLevelWeight(LogLevel level) -> int
{
  switch (level)
  {
    case LogLevel::kDebug:
      return kLogLevelWeightDebug;
    case LogLevel::kInfo:
      return kLogLevelWeightInfo;
    case LogLevel::kNotice:
      return kLogLevelWeightNotice;
    case LogLevel::kWarning:
      return kLogLevelWeightWarning;
    case LogLevel::kError:
      return kLogLevelWeightError;
    case LogLevel::kCritical:
      return kLogLevelWeightCritical;
    case LogLevel::kAlert:
      return kLogLevelWeightAlert;
    case LogLevel::kEmergency:
      return kLogLevelWeightEmergency;
  }

  return kLogLevelWeightDebug;
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

auto endpointName(ListEndpoint endpoint) -> std::string_view
{
  switch (endpoint)
  {
    case ListEndpoint::kTools:
      return "tools";
    case ListEndpoint::kResources:
      return "resources";
    case ListEndpoint::kResourceTemplates:
      return "resource_templates";
    case ListEndpoint::kPrompts:
      return "prompts";
    case ListEndpoint::kTasks:
      return "tasks";
  }

  return "unknown";
}

auto validateParamsObject(const jsonrpc::Request &request, std::string_view methodName) -> std::optional<jsonrpc::Response>
{
  if (!request.params.has_value() || !request.params->is_object())
  {
    return detail::makeInvalidParamsResponse(request.id, std::string(methodName) + " requires params object");
  }
  return std::nullopt;
}

template<typename Container, typename GetNameFn>
auto throwIfDuplicateExists(const Container &container, std::string_view name, GetNameFn &&getNameFn, std::string_view itemType) -> void
{
  const auto existing = std::find_if(container.begin(), container.end(), [&name, &getNameFn](const auto &item) -> bool { return getNameFn(item) == name; });

  if (existing != container.end())
  {
    throw std::invalid_argument(std::string(itemType) + " already registered: " + std::string(name));
  }
}

}  // namespace mcp::server::detail
