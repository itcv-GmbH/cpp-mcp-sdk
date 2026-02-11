#pragma once

#include <functional>
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

struct CallToolResult
{
  jsonrpc::JsonValue content = jsonrpc::JsonValue::array();
  std::optional<jsonrpc::JsonValue> structuredContent;
  bool isError = false;
  std::optional<jsonrpc::JsonValue> metadata;
};

struct ToolCallContext
{
  jsonrpc::RequestContext requestContext;
  std::string toolName;
  jsonrpc::JsonValue arguments = jsonrpc::JsonValue::object();
};

using ToolHandler = std::function<CallToolResult(const ToolCallContext &)>;

struct RegisteredTool
{
  ToolDefinition definition;
  ToolHandler handler;
};

}  // namespace mcp
