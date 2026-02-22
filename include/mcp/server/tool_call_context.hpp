#pragma once

#include <string>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{

struct ToolCallContext
{
  jsonrpc::RequestContext requestContext;
  std::string toolName;
  jsonrpc::JsonValue arguments = jsonrpc::JsonValue::object();
};

}  // namespace mcp
