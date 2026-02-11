#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include <jsoncons/json.hpp>
#include <mcp/errors.hpp>
#include <mcp/version.hpp>

namespace mcp::jsonrpc
{

using RequestId = std::variant<std::int64_t, std::string>;
using JsonValue = jsoncons::json;

struct EncodeOptions
{
  bool disallowEmbeddedNewlines = false;
};

class MessageValidationError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

struct RequestContext
{
  std::string protocolVersion = std::string(kLatestProtocolVersion);
  std::optional<std::string> sessionId;
};

struct Request
{
  std::string jsonrpc = std::string(kJsonRpcVersion);
  RequestId id = std::int64_t {0};
  std::string method;
  std::optional<JsonValue> params;
  JsonValue additionalProperties = JsonValue::object();
};

struct Notification
{
  std::string jsonrpc = std::string(kJsonRpcVersion);
  std::string method;
  std::optional<JsonValue> params;
  JsonValue additionalProperties = JsonValue::object();
};

struct SuccessResponse
{
  std::string jsonrpc = std::string(kJsonRpcVersion);
  RequestId id = std::int64_t {0};
  JsonValue result;
  JsonValue additionalProperties = JsonValue::object();
};

struct ErrorResponse
{
  std::string jsonrpc = std::string(kJsonRpcVersion);
  std::optional<RequestId> id;
  bool hasUnknownId = false;
  JsonRpcError error;
  JsonValue additionalProperties = JsonValue::object();
};

using Response = std::variant<SuccessResponse, ErrorResponse>;
using Message = std::variant<Request, Notification, SuccessResponse, ErrorResponse>;

auto parseMessage(std::string_view utf8Json) -> Message;
auto parseMessageJson(const JsonValue &messageJson) -> Message;

auto toJson(const Message &message) -> JsonValue;
auto serializeMessage(const Message &message, const EncodeOptions &options = {}) -> std::string;

auto makeJsonRpcError(JsonRpcErrorCode code, std::string message, std::optional<JsonValue> data = std::nullopt) -> JsonRpcError;
auto makeParseError(std::optional<JsonValue> data = std::nullopt, std::string message = "Parse error") -> JsonRpcError;
auto makeInvalidRequestError(std::optional<JsonValue> data = std::nullopt, std::string message = "Invalid Request") -> JsonRpcError;
auto makeMethodNotFoundError(std::optional<JsonValue> data = std::nullopt, std::string message = "Method not found") -> JsonRpcError;
auto makeInvalidParamsError(std::optional<JsonValue> data = std::nullopt, std::string message = "Invalid params") -> JsonRpcError;
auto makeInternalError(std::optional<JsonValue> data = std::nullopt, std::string message = "Internal error") -> JsonRpcError;
auto makeUrlElicitationRequiredError(std::optional<JsonValue> data = std::nullopt, std::string message = "URL elicitation required") -> JsonRpcError;

auto makeErrorResponse(JsonRpcError error, std::optional<RequestId> id = std::nullopt) -> ErrorResponse;
auto makeUnknownIdErrorResponse(JsonRpcError error) -> ErrorResponse;

}  // namespace mcp::jsonrpc
