#pragma once

#include <any>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include <mcp/errors.hpp>
#include <mcp/version.hpp>

namespace mcp
{
namespace jsonrpc
{

using RequestId = std::variant<std::int64_t, std::string>;
using JsonValue = std::any;

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
};

struct Notification
{
  std::string jsonrpc = std::string(kJsonRpcVersion);
  std::string method;
  std::optional<JsonValue> params;
};

struct SuccessResponse
{
  std::string jsonrpc = std::string(kJsonRpcVersion);
  RequestId id = std::int64_t {0};
  JsonValue result;
};

struct ErrorResponse
{
  std::string jsonrpc = std::string(kJsonRpcVersion);
  std::optional<RequestId> id;
  JsonRpcError error;
};

using Response = std::variant<SuccessResponse, ErrorResponse>;
using Message = std::variant<Request, Notification, SuccessResponse, ErrorResponse>;

}  // namespace jsonrpc
}  // namespace mcp
