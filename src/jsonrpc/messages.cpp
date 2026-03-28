#include <algorithm>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "mcp/jsonrpc/message.hpp"

#include "mcp/jsonrpc/encode_options.hpp"
#include "mcp/jsonrpc/error_response.hpp"
#include "mcp/jsonrpc/message_functions.hpp"
#include "mcp/jsonrpc/message_validation_error.hpp"
#include "mcp/jsonrpc/notification.hpp"
#include "mcp/jsonrpc/request.hpp"
#include "mcp/jsonrpc/success_response.hpp"
#include "mcp/jsonrpc/types.hpp"
#include "mcp/schema/format_diagnostics.hpp"
#include "mcp/schema/validation_result.hpp"
#include "mcp/schema/validator.hpp"
#include "mcp/sdk/errors.hpp"
#include "mcp/sdk/version.hpp"

namespace mcp::jsonrpc
{
namespace detail
{

static auto ensureMessageObject(const JsonValue &messageJson) -> void
{
  if (!messageJson.is_object())
  {
    throw MessageValidationError("JSON-RPC message must be a JSON object.");
  }
}

static auto ensureJsonRpcVersion(const JsonValue &messageJson) -> void
{
  if (!messageJson.contains("jsonrpc"))
  {
    throw MessageValidationError("JSON-RPC message is missing required field 'jsonrpc'.");
  }

  const JsonValue &jsonrpcValue = messageJson.at("jsonrpc");
  if (!jsonrpcValue.is_string())
  {
    throw MessageValidationError("JSON-RPC field 'jsonrpc' must be a string.");
  }

  if (jsonrpcValue.as<std::string>() != kJsonRpcVersion)
  {
    throw MessageValidationError("JSON-RPC field 'jsonrpc' must be exactly '2.0'.");
  }
}

static auto parseRequestId(const JsonValue &idValue) -> RequestId
{
  if (idValue.is_string())
  {
    return idValue.as<std::string>();
  }

  if (idValue.is_int64())
  {
    return idValue.as<std::int64_t>();
  }

  if (idValue.is_uint64())
  {
    const std::uint64_t unsignedValue = idValue.as<std::uint64_t>();
    if (unsignedValue <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
      return static_cast<std::int64_t>(unsignedValue);
    }
  }

  throw MessageValidationError("JSON-RPC field 'id' must be a string or integer.");
}

static auto parseErrorCode(const JsonValue &codeValue) -> std::int32_t
{
  if (codeValue.is_int64())
  {
    const std::int64_t signedCode = codeValue.as<std::int64_t>();
    if (signedCode < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) || signedCode > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()))
    {
      throw MessageValidationError("JSON-RPC error code is out of range for int32.");
    }

    return static_cast<std::int32_t>(signedCode);
  }

  if (codeValue.is_uint64())
  {
    const std::uint64_t unsignedCode = codeValue.as<std::uint64_t>();
    if (unsignedCode <= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
    {
      return static_cast<std::int32_t>(unsignedCode);
    }

    throw MessageValidationError("JSON-RPC error code is out of range for int32.");
  }

  throw MessageValidationError("JSON-RPC error field 'code' must be an integer.");
}

static auto parseErrorObject(const JsonValue &errorValue) -> JsonRpcError
{
  if (!errorValue.is_object())
  {
    throw MessageValidationError("JSON-RPC field 'error' must be an object.");
  }

  if (!errorValue.contains("code") || !errorValue.contains("message"))
  {
    throw MessageValidationError("JSON-RPC error object must include 'code' and 'message'.");
  }

  const JsonValue &codeValue = errorValue.at("code");
  const JsonValue &messageValue = errorValue.at("message");

  if (!messageValue.is_string())
  {
    throw MessageValidationError("JSON-RPC error field 'message' must be a string.");
  }

  JsonRpcError error;
  error.code = parseErrorCode(codeValue);
  error.message = messageValue.as<std::string>();

  if (errorValue.contains("data"))
  {
    error.data = errorValue.at("data");
  }

  return error;
}

static auto extractAdditionalProperties(const JsonValue &messageJson, std::initializer_list<const char *> knownFields) -> JsonValue
{
  JsonValue additionalProperties = messageJson;
  for (const char *knownField : knownFields)
  {
    additionalProperties.erase(knownField);
  }

  return additionalProperties;
}

static auto requestIdToJson(const RequestId &id) -> JsonValue
{
  if (std::holds_alternative<std::int64_t>(id))
  {
    return {std::get<std::int64_t>(id)};
  }

  return {std::get<std::string>(id)};
}

static auto ensureAdditionalPropertiesObject(const JsonValue &additionalProperties, const char *context) -> void
{
  if (!additionalProperties.is_object())
  {
    throw MessageValidationError(std::string(context) + " additionalProperties must be a JSON object.");
  }
}

static auto ensureJsonRpcVersionField(const std::string &jsonrpc) -> void
{
  if (jsonrpc != kJsonRpcVersion)
  {
    throw MessageValidationError("JSON-RPC field 'jsonrpc' must be exactly '2.0'.");
  }
}

static auto errorToJson(const JsonRpcError &error) -> JsonValue
{
  JsonValue errorJson = JsonValue::object();
  errorJson["code"] = error.code;
  errorJson["message"] = error.message;

  if (error.data.has_value())
  {
    errorJson["data"] = *error.data;
  }

  return errorJson;
}

static auto validateNoEmbeddedNewlines(const std::string &serializedMessage) -> void
{
  const bool hasNewlines = std::any_of(serializedMessage.begin(), serializedMessage.end(), [](char value) -> bool { return value == '\n' || value == '\r'; });

  if (hasNewlines)
  {
    throw MessageValidationError("Serialized JSON-RPC message contains embedded newlines.");
  }
}

static auto requestToJson(const Request &request) -> JsonValue
{
  ensureJsonRpcVersionField(request.jsonrpc);
  ensureAdditionalPropertiesObject(request.additionalProperties, "Request");

  JsonValue messageJson = request.additionalProperties;
  messageJson.erase("jsonrpc");
  messageJson.erase("id");
  messageJson.erase("method");
  messageJson.erase("params");

  messageJson["jsonrpc"] = request.jsonrpc;
  messageJson["id"] = requestIdToJson(request.id);
  messageJson["method"] = request.method;
  if (request.params.has_value())
  {
    messageJson["params"] = *request.params;
  }

  return messageJson;
}

static auto notificationToJson(const Notification &notification) -> JsonValue
{
  ensureJsonRpcVersionField(notification.jsonrpc);
  ensureAdditionalPropertiesObject(notification.additionalProperties, "Notification");

  JsonValue messageJson = notification.additionalProperties;
  messageJson.erase("jsonrpc");
  messageJson.erase("method");
  messageJson.erase("params");
  messageJson.erase("id");

  messageJson["jsonrpc"] = notification.jsonrpc;
  messageJson["method"] = notification.method;
  if (notification.params.has_value())
  {
    messageJson["params"] = *notification.params;
  }

  return messageJson;
}

static auto successResponseToJson(const SuccessResponse &response) -> JsonValue
{
  ensureJsonRpcVersionField(response.jsonrpc);
  ensureAdditionalPropertiesObject(response.additionalProperties, "SuccessResponse");

  JsonValue messageJson = response.additionalProperties;
  messageJson.erase("jsonrpc");
  messageJson.erase("id");
  messageJson.erase("result");
  messageJson.erase("error");

  messageJson["jsonrpc"] = response.jsonrpc;
  messageJson["id"] = requestIdToJson(response.id);
  messageJson["result"] = response.result;

  return messageJson;
}

static auto errorResponseToJson(const ErrorResponse &response) -> JsonValue
{
  ensureJsonRpcVersionField(response.jsonrpc);
  ensureAdditionalPropertiesObject(response.additionalProperties, "ErrorResponse");

  if (response.hasUnknownId && response.id.has_value())
  {
    throw MessageValidationError("ErrorResponse cannot set both a concrete 'id' and unknown-id state.");
  }

  JsonValue messageJson = response.additionalProperties;
  messageJson.erase("jsonrpc");
  messageJson.erase("id");
  messageJson.erase("result");
  messageJson.erase("error");

  messageJson["jsonrpc"] = response.jsonrpc;
  if (response.id.has_value())
  {
    messageJson["id"] = requestIdToJson(*response.id);
  }
  else if (response.hasUnknownId)
  {
    messageJson["id"] = JsonValue::null();
  }

  messageJson["error"] = errorToJson(response.error);

  return messageJson;
}

static auto parseRequestMessage(const JsonValue &messageJson, const JsonValue &methodValue) -> Request
{
  const JsonValue &idValue = messageJson.at("id");
  if (idValue.is_null())
  {
    throw MessageValidationError("JSON-RPC request 'id' must be non-null.");
  }

  Request request;
  request.id = parseRequestId(idValue);
  request.method = methodValue.as<std::string>();
  if (messageJson.contains("params"))
  {
    request.params = messageJson.at("params");
  }

  request.additionalProperties = extractAdditionalProperties(messageJson, {"jsonrpc", "id", "method", "params"});
  return request;
}

static auto parseNotificationMessage(const JsonValue &messageJson, const JsonValue &methodValue) -> Notification
{
  Notification notification;
  notification.method = methodValue.as<std::string>();
  if (messageJson.contains("params"))
  {
    notification.params = messageJson.at("params");
  }

  notification.additionalProperties = extractAdditionalProperties(messageJson, {"jsonrpc", "method", "params"});
  return notification;
}

static auto parseRequestOrNotification(const JsonValue &messageJson, bool hasResult, bool hasError) -> Message
{
  if (hasResult || hasError)
  {
    throw MessageValidationError("JSON-RPC request/notification must not include 'result' or 'error'.");
  }

  const JsonValue &methodValue = messageJson.at("method");
  if (!methodValue.is_string())
  {
    throw MessageValidationError("JSON-RPC field 'method' must be a string.");
  }

  if (messageJson.contains("id"))
  {
    return parseRequestMessage(messageJson, methodValue);
  }

  return parseNotificationMessage(messageJson, methodValue);
}

static auto parseSuccessResponseMessage(const JsonValue &messageJson) -> SuccessResponse
{
  if (!messageJson.contains("id"))
  {
    throw MessageValidationError("JSON-RPC success response must include an 'id'.");
  }

  const JsonValue &idValue = messageJson.at("id");
  if (idValue.is_null())
  {
    throw MessageValidationError("JSON-RPC success response 'id' must be non-null.");
  }

  SuccessResponse response;
  response.id = parseRequestId(idValue);
  response.result = messageJson.at("result");
  response.additionalProperties = extractAdditionalProperties(messageJson, {"jsonrpc", "id", "result"});
  return response;
}

static auto parseErrorResponseMessage(const JsonValue &messageJson) -> ErrorResponse
{
  ErrorResponse response;
  if (messageJson.contains("id"))
  {
    const JsonValue &idValue = messageJson.at("id");
    if (idValue.is_null())
    {
      response.hasUnknownId = true;
    }
    else
    {
      response.id = parseRequestId(idValue);
    }
  }

  response.error = parseErrorObject(messageJson.at("error"));
  response.additionalProperties = extractAdditionalProperties(messageJson, {"jsonrpc", "id", "error"});
  return response;
}

static auto parseResponseMessage(const JsonValue &messageJson, bool hasResult, bool hasError) -> Message
{
  if (messageJson.contains("params"))
  {
    throw MessageValidationError("JSON-RPC response must not include 'params'.");
  }

  if (hasResult == hasError)
  {
    throw MessageValidationError("JSON-RPC response must include exactly one of 'result' or 'error'.");
  }

  if (hasResult)
  {
    return parseSuccessResponseMessage(messageJson);
  }

  return parseErrorResponseMessage(messageJson);
}

static auto mcpSchemaValidator() -> const mcp::schema::Validator &
{
  static const mcp::schema::Validator validator = mcp::schema::Validator::loadPinnedMcpSchema();
  return validator;
}

static auto ensureMcpMethodMessageSchema(const JsonValue &messageJson) -> void
{
  if (!messageJson.contains("method"))
  {
    return;
  }

  const mcp::schema::ValidationResult validationResult = mcpSchemaValidator().validateMcpMethodMessage(messageJson);
  if (!validationResult.valid)
  {
    throw MessageValidationError("MCP schema validation failed: " + mcp::schema::formatDiagnostics(validationResult));
  }
}

}  // namespace detail

auto parseMessage(std::string_view utf8Json) -> Message
{
  try
  {
    const JsonValue messageJson = JsonValue::parse(std::string(utf8Json));
    return parseMessageJson(messageJson);
  }
  catch (const MessageValidationError &)
  {
    throw;
  }
  catch (const std::exception &exception)
  {
    throw MessageValidationError(std::string("Failed to parse JSON-RPC message: ") + exception.what());
  }
}

auto parseMessageJson(const JsonValue &messageJson) -> Message
{
  detail::ensureMessageObject(messageJson);
  detail::ensureJsonRpcVersion(messageJson);

  try
  {
    detail::ensureMcpMethodMessageSchema(messageJson);
  }
  catch (const MessageValidationError &)
  {
    throw;
  }
  catch (const std::exception &exception)
  {
    throw MessageValidationError(std::string("Failed to validate MCP message against schema: ") + exception.what());
  }

  const bool hasMethod = messageJson.contains("method");
  const bool hasResult = messageJson.contains("result");
  const bool hasError = messageJson.contains("error");

  if (hasMethod)
  {
    return detail::parseRequestOrNotification(messageJson, hasResult, hasError);
  }

  return detail::parseResponseMessage(messageJson, hasResult, hasError);
}

auto toJson(const Message &message) -> JsonValue
{
  return std::visit(
    [](const auto &typedMessage) -> JsonValue
    {
      using MessageType = std::decay_t<decltype(typedMessage)>;

      if constexpr (std::is_same_v<MessageType, Request>)
      {
        return detail::requestToJson(typedMessage);
      }
      else if constexpr (std::is_same_v<MessageType, Notification>)
      {
        return detail::notificationToJson(typedMessage);
      }
      else if constexpr (std::is_same_v<MessageType, SuccessResponse>)
      {
        return detail::successResponseToJson(typedMessage);
      }
      else
      {
        return detail::errorResponseToJson(typedMessage);
      }
    },
    message);
}

auto serializeMessage(const Message &message, const EncodeOptions &options) -> std::string
{
  const JsonValue messageJson = toJson(message);

  std::string serializedMessage;
  messageJson.dump(serializedMessage);

  if (options.disallowEmbeddedNewlines)
  {
    detail::validateNoEmbeddedNewlines(serializedMessage);
  }

  return serializedMessage;
}

MCP_SDK_EXPORT auto makeJsonRpcError(JsonRpcErrorCode code, std::string message, std::optional<JsonValue> data) -> JsonRpcError
{
  JsonRpcError error;
  error.code = static_cast<std::int32_t>(code);
  error.message = std::move(message);
  error.data = std::move(data);
  return error;
}

MCP_SDK_EXPORT auto makeParseError(std::optional<JsonValue> data, std::string message) -> JsonRpcError
{
  return makeJsonRpcError(JsonRpcErrorCode::kParseError, std::move(message), std::move(data));
}

MCP_SDK_EXPORT auto makeInvalidRequestError(std::optional<JsonValue> data, std::string message) -> JsonRpcError
{
  return makeJsonRpcError(JsonRpcErrorCode::kInvalidRequest, std::move(message), std::move(data));
}

MCP_SDK_EXPORT auto makeMethodNotFoundError(std::optional<JsonValue> data, std::string message) -> JsonRpcError
{
  return makeJsonRpcError(JsonRpcErrorCode::kMethodNotFound, std::move(message), std::move(data));
}

MCP_SDK_EXPORT auto makeInvalidParamsError(std::optional<JsonValue> data, std::string message) -> JsonRpcError
{
  return makeJsonRpcError(JsonRpcErrorCode::kInvalidParams, std::move(message), std::move(data));
}

MCP_SDK_EXPORT auto makeInternalError(std::optional<JsonValue> data, std::string message) -> JsonRpcError
{
  return makeJsonRpcError(JsonRpcErrorCode::kInternalError, std::move(message), std::move(data));
}

MCP_SDK_EXPORT auto makeUrlElicitationRequiredError(std::optional<JsonValue> data, std::string message) -> JsonRpcError
{
  return makeJsonRpcError(JsonRpcErrorCode::kUrlElicitationRequired, std::move(message), std::move(data));
}

MCP_SDK_EXPORT auto makeErrorResponse(JsonRpcError error, std::optional<RequestId> id) -> ErrorResponse
{
  ErrorResponse response;
  response.id = std::move(id);
  response.error = std::move(error);
  response.hasUnknownId = false;
  return response;
}

MCP_SDK_EXPORT auto makeUnknownIdErrorResponse(JsonRpcError error) -> ErrorResponse
{
  ErrorResponse response;
  response.id = std::nullopt;
  response.hasUnknownId = true;
  response.error = std::move(error);
  return response;
}

}  // namespace mcp::jsonrpc
