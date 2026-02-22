#pragma once

#include <optional>
#include <string>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{

struct ToolDefinition
{
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<jsonrpc::JsonValue> icons;
  jsonrpc::JsonValue inputSchema = jsonrpc::JsonValue::object();
  std::optional<jsonrpc::JsonValue> outputSchema;
  std::optional<jsonrpc::JsonValue> annotations;
  std::optional<jsonrpc::JsonValue> execution;
  std::optional<jsonrpc::JsonValue> metadata;
};

}  // namespace mcp
