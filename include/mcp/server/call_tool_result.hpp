#pragma once

#include <optional>

#include <mcp/jsonrpc/all.hpp>

namespace mcp
{

struct CallToolResult
{
  jsonrpc::JsonValue content = jsonrpc::JsonValue::array();
  std::optional<jsonrpc::JsonValue> structuredContent;
  bool isError = false;
  std::optional<jsonrpc::JsonValue> metadata;
};

}  // namespace mcp
