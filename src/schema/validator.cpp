#include <array>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <jsoncons_ext/jsonschema/common/validator.hpp>
#include <jsoncons_ext/jsonschema/evaluation_options.hpp>
#include <jsoncons_ext/jsonschema/json_schema.hpp>
#include <jsoncons_ext/jsonschema/json_schema_factory.hpp>
#include <jsoncons_ext/jsonschema/validation_message.hpp>
#include <mcp/schema/detail/pinned_schema.hpp>
#include <mcp/schema/validator.hpp>

namespace mcp::schema
{
using JsonSchema = jsoncons::jsonschema::json_schema<JsonValue>;
using WalkResult = jsoncons::jsonschema::walk_result;

constexpr std::string_view kPinnedSchemaUpstreamUrl = "https://raw.githubusercontent.com/modelcontextprotocol/specification/main/schema/2025-11-25/schema.json";
constexpr std::string_view kPinnedSchemaUpstreamRef = "2025-11-25";
constexpr std::string_view kPinnedSchemaSha256 = "1ffe4c5577974012f5fa02af14ea88df4b7146679df1abaaad497c8d9230ca8a";

static auto makeInvalidResult(std::string message, std::string instanceLocation = {}, std::string evaluationPath = {}, std::string schemaLocation = {}) -> ValidationResult
{
  ValidationResult result;
  result.valid = false;

  ValidationDiagnostic diagnostic;
  diagnostic.message = std::move(message);
  diagnostic.instanceLocation = std::move(instanceLocation);
  diagnostic.evaluationPath = std::move(evaluationPath);
  diagnostic.schemaLocation = std::move(schemaLocation);
  result.diagnostics.push_back(std::move(diagnostic));

  return result;
}

static auto normalizeDialectIdentifier(std::string dialectIdentifier) -> std::string
{
  if (!dialectIdentifier.empty() && dialectIdentifier.back() == '#')
  {
    dialectIdentifier.pop_back();
  }

  return dialectIdentifier;
}

static auto canonicalDialectIdentifier(const std::string &dialectIdentifier) -> std::string
{
  const std::string normalized = normalizeDialectIdentifier(dialectIdentifier);

  if (normalized == normalizeDialectIdentifier(jsoncons::jsonschema::schema_version::draft4()))
  {
    return jsoncons::jsonschema::schema_version::draft4();
  }

  if (normalized == normalizeDialectIdentifier(jsoncons::jsonschema::schema_version::draft6()))
  {
    return jsoncons::jsonschema::schema_version::draft6();
  }

  if (normalized == normalizeDialectIdentifier(jsoncons::jsonschema::schema_version::draft7()) || normalized == "https://json-schema.org/draft-07/schema")
  {
    return jsoncons::jsonschema::schema_version::draft7();
  }

  if (normalized == normalizeDialectIdentifier(jsoncons::jsonschema::schema_version::draft201909()))
  {
    return jsoncons::jsonschema::schema_version::draft201909();
  }

  if (normalized == normalizeDialectIdentifier(jsoncons::jsonschema::schema_version::draft202012()))
  {
    return jsoncons::jsonschema::schema_version::draft202012();
  }

  return {};
}

static auto resolveDialect(const JsonValue &schemaObject) -> ValidationResult
{
  ValidationResult result;
  result.valid = true;
  result.effectiveDialect = jsoncons::jsonschema::schema_version::draft202012();

  if (!schemaObject.contains("$schema"))
  {
    return result;
  }

  const JsonValue &dialectValue = schemaObject.at("$schema");
  if (!dialectValue.is_string())
  {
    return makeInvalidResult("JSON Schema '$schema' value must be a string.", "/$schema", "/$schema");
  }

  const std::string resolvedDialect = canonicalDialectIdentifier(dialectValue.as<std::string>());
  if (resolvedDialect.empty())
  {
    return makeInvalidResult("Unsupported JSON Schema dialect: '" + dialectValue.as<std::string>() + "'.", "/$schema", "/$schema");
  }

  result.effectiveDialect = resolvedDialect;
  return result;
}

static auto validateAgainstCompiledSchema(const JsonValue &schemaObject, const JsonValue &instance, const std::string &effectiveDialect) -> ValidationResult
{
  ValidationResult result;
  result.valid = true;
  result.effectiveDialect = effectiveDialect;

  try
  {
    const jsoncons::jsonschema::evaluation_options options = jsoncons::jsonschema::evaluation_options {}.default_version(effectiveDialect);
    const JsonSchema compiledSchema = jsoncons::jsonschema::make_json_schema(JsonValue(schemaObject), options);

    compiledSchema.validate(instance,
                            [&result](const jsoncons::jsonschema::validation_message &message) -> WalkResult
                            {
                              ValidationDiagnostic diagnostic;
                              diagnostic.instanceLocation = message.instance_location().string();
                              diagnostic.evaluationPath = message.eval_path().string();
                              diagnostic.schemaLocation = message.schema_location().string();
                              diagnostic.message = message.message();
                              result.diagnostics.push_back(std::move(diagnostic));
                              return WalkResult::advance;
                            });
  }
  catch (const std::exception &exception)
  {
    return makeInvalidResult(std::string("Schema compilation failed: ") + exception.what());
  }

  result.valid = result.diagnostics.empty();
  return result;
}

static auto methodDefinitionName(std::string_view methodName) -> std::string_view
{
  static const std::array<std::pair<std::string_view, std::string_view>, 31> kMethodDefinitions {{
    std::pair<std::string_view, std::string_view> {"initialize", "InitializeRequest"},
    std::pair<std::string_view, std::string_view> {"ping", "PingRequest"},
    std::pair<std::string_view, std::string_view> {"tools/list", "ListToolsRequest"},
    std::pair<std::string_view, std::string_view> {"tools/call", "CallToolRequest"},
    std::pair<std::string_view, std::string_view> {"resources/list", "ListResourcesRequest"},
    std::pair<std::string_view, std::string_view> {"resources/read", "ReadResourceRequest"},
    std::pair<std::string_view, std::string_view> {"resources/templates/list", "ListResourceTemplatesRequest"},
    std::pair<std::string_view, std::string_view> {"resources/subscribe", "SubscribeRequest"},
    std::pair<std::string_view, std::string_view> {"resources/unsubscribe", "UnsubscribeRequest"},
    std::pair<std::string_view, std::string_view> {"prompts/list", "ListPromptsRequest"},
    std::pair<std::string_view, std::string_view> {"prompts/get", "GetPromptRequest"},
    std::pair<std::string_view, std::string_view> {"logging/setLevel", "SetLevelRequest"},
    std::pair<std::string_view, std::string_view> {"completion/complete", "CompleteRequest"},
    std::pair<std::string_view, std::string_view> {"roots/list", "ListRootsRequest"},
    std::pair<std::string_view, std::string_view> {"sampling/createMessage", "CreateMessageRequest"},
    std::pair<std::string_view, std::string_view> {"elicitation/create", "ElicitRequest"},
    std::pair<std::string_view, std::string_view> {"tasks/get", "GetTaskRequest"},
    std::pair<std::string_view, std::string_view> {"tasks/result", "GetTaskPayloadRequest"},
    std::pair<std::string_view, std::string_view> {"tasks/list", "ListTasksRequest"},
    std::pair<std::string_view, std::string_view> {"tasks/cancel", "CancelTaskRequest"},
    std::pair<std::string_view, std::string_view> {"notifications/initialized", "InitializedNotification"},
    std::pair<std::string_view, std::string_view> {"notifications/cancelled", "CancelledNotification"},
    std::pair<std::string_view, std::string_view> {"notifications/progress", "ProgressNotification"},
    std::pair<std::string_view, std::string_view> {"notifications/message", "LoggingMessageNotification"},
    std::pair<std::string_view, std::string_view> {"notifications/tools/list_changed", "ToolListChangedNotification"},
    std::pair<std::string_view, std::string_view> {"notifications/resources/list_changed", "ResourceListChangedNotification"},
    std::pair<std::string_view, std::string_view> {"notifications/resources/updated", "ResourceUpdatedNotification"},
    std::pair<std::string_view, std::string_view> {"notifications/prompts/list_changed", "PromptListChangedNotification"},
    std::pair<std::string_view, std::string_view> {"notifications/roots/list_changed", "RootsListChangedNotification"},
    std::pair<std::string_view, std::string_view> {"notifications/tasks/status", "TaskStatusNotification"},
    std::pair<std::string_view, std::string_view> {"notifications/elicitation/complete", "ElicitationCompleteNotification"},
  }};

  for (const auto &entry : kMethodDefinitions)
  {
    if (entry.first == methodName)
    {
      return entry.second;
    }
  }

  return {};
}

static auto diagnosticsJson(const ValidationResult &result) -> JsonValue
{
  JsonValue diagnostics = JsonValue::array();

  for (const ValidationDiagnostic &diagnostic : result.diagnostics)
  {
    JsonValue diagnosticJson = JsonValue::object();
    diagnosticJson["instanceLocation"] = diagnostic.instanceLocation;
    diagnosticJson["evaluationPath"] = diagnostic.evaluationPath;
    diagnosticJson["schemaLocation"] = diagnostic.schemaLocation;
    diagnosticJson["error"] = diagnostic.message;
    diagnostics.push_back(std::move(diagnosticJson));
  }

  return diagnostics;
}

auto Validator::loadPinnedMcpSchema() -> Validator
{
  Validator validator;

  try
  {
    validator.schemaDocument_ = JsonValue::parse(std::string(detail::pinnedSchemaJson()));
  }
  catch (const std::exception &exception)
  {
    throw std::runtime_error(std::string("Failed to parse embedded pinned MCP schema: ") + exception.what());
  }

  validator.metadata_.localPath = std::string(detail::pinnedSchemaSourcePath());
  validator.metadata_.upstreamSchemaUrl = std::string(kPinnedSchemaUpstreamUrl);
  validator.metadata_.upstreamRef = std::string(kPinnedSchemaUpstreamRef);
  validator.metadata_.sha256 = std::string(kPinnedSchemaSha256);
  return validator;
}

auto Validator::validateInstance(const JsonValue &instance, std::string_view definitionName) const -> ValidationResult
{
  if (!schemaDocument_.is_object())
  {
    return makeInvalidResult("Pinned MCP schema document must be a JSON object.");
  }

  if (!schemaDocument_.contains("$defs") || !schemaDocument_.at("$defs").is_object())
  {
    return makeInvalidResult("Pinned MCP schema document is missing an object '$defs'.");
  }

  const JsonValue &definitions = schemaDocument_.at("$defs");
  const std::string definitionNameString(definitionName);
  if (!definitions.contains(definitionNameString))
  {
    return makeInvalidResult("Pinned MCP schema does not contain definition '#/$defs/" + definitionNameString + "'.");
  }

  JsonValue definitionSchema = JsonValue::object();
  definitionSchema["$schema"] = jsoncons::jsonschema::schema_version::draft202012();
  definitionSchema["$defs"] = definitions;
  definitionSchema["$ref"] = "#/$defs/" + definitionNameString;

  return validateAgainstCompiledSchema(definitionSchema, instance, jsoncons::jsonschema::schema_version::draft202012());
}

auto Validator::validateMcpMethodMessage(const JsonValue &messageJson) const -> ValidationResult
{
  if (!messageJson.is_object())
  {
    return makeInvalidResult("MCP message must be a JSON object.");
  }

  if (!messageJson.contains("method") || !messageJson.at("method").is_string())
  {
    return validateInstance(messageJson, "JSONRPCMessage");
  }

  const std::string methodName = messageJson.at("method").as<std::string>();
  const std::string_view definitionName = methodDefinitionName(methodName);
  if (!definitionName.empty())
  {
    return validateInstance(messageJson, definitionName);
  }

  if (messageJson.contains("id"))
  {
    return validateInstance(messageJson, "JSONRPCRequest");
  }

  return validateInstance(messageJson, "JSONRPCNotification");
}

auto Validator::validateToolSchema(const JsonValue &schemaObject, ToolSchemaKind kind) const -> ValidationResult
{
  if (!schemaObject.is_object())
  {
    return makeInvalidResult("Tool schema must be a JSON object.");
  }

  ValidationResult dialectResult = resolveDialect(schemaObject);
  if (!dialectResult.valid)
  {
    return dialectResult;
  }

  try
  {
    const jsoncons::jsonschema::evaluation_options options = jsoncons::jsonschema::evaluation_options {}.default_version(dialectResult.effectiveDialect);
    (void)jsoncons::jsonschema::make_json_schema(JsonValue(schemaObject), options);
  }
  catch (const std::exception &exception)
  {
    ValidationResult result = makeInvalidResult(std::string("Schema compilation failed: ") + exception.what());
    result.effectiveDialect = dialectResult.effectiveDialect;
    return result;
  }

  JsonValue toolObject = JsonValue::object();
  toolObject["name"] = "tool";
  toolObject["inputSchema"] = JsonValue::object();
  toolObject["inputSchema"]["type"] = "object";
  if (kind == ToolSchemaKind::kInput)
  {
    toolObject["inputSchema"] = schemaObject;
  }
  else
  {
    toolObject["outputSchema"] = schemaObject;
  }

  ValidationResult result = validateInstance(toolObject, "Tool");
  result.effectiveDialect = dialectResult.effectiveDialect;
  return result;
}

auto Validator::metadata() const noexcept -> const PinnedSchemaMetadata &
{
  return metadata_;
}

auto formatDiagnostics(const ValidationResult &result) -> std::string
{
  std::string encodedDiagnostics;
  diagnosticsJson(result).dump(encodedDiagnostics);
  return encodedDiagnostics;
}

}  // namespace mcp::schema
