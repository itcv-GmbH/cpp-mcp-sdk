#pragma once

#include <string>

#include <mcp/jsonrpc/all.hpp>

namespace mcp::server
{

struct ToolCallContext
{
  jsonrpc::RequestContext requestContext;
  std::string toolName;
  jsonrpc::JsonValue arguments = jsonrpc::JsonValue::object();
};

}  // namespace mcp::server
