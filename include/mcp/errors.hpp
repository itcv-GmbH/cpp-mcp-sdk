#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <jsoncons/json.hpp>

namespace mcp
{

enum class JsonRpcErrorCode : std::int32_t
{
  kParseError = -32700,
  kInvalidRequest = -32600,
  kMethodNotFound = -32601,
  kInvalidParams = -32602,
  kInternalError = -32603,
  kResourceNotFound = -32002,
  kUrlElicitationRequired = -32042,
};

struct JsonRpcError
{
  std::int32_t code = static_cast<std::int32_t>(JsonRpcErrorCode::kInternalError);
  std::string message;
  std::optional<jsoncons::json> data;
};

}  // namespace mcp
