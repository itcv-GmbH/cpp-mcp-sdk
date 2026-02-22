#pragma once

#include <optional>
#include <string>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{

struct FormElicitationRequest
{
  std::string message;
  jsonrpc::JsonValue requestedSchema;
  std::optional<jsonrpc::JsonValue> metadata;
};

}  // namespace mcp
