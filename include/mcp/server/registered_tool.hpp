#pragma once

#include <mcp/server/tool_definition.hpp>
#include <mcp/server/tool_handler.hpp>

namespace mcp::server
{

struct RegisteredTool
{
  ToolDefinition definition;
  ToolHandler handler;
};

}  // namespace mcp::server
