#pragma once

#include <functional>

#include <mcp/server/call_tool_result.hpp>
#include <mcp/server/tool_call_context.hpp>

namespace mcp
{

using ToolHandler = std::function<CallToolResult(const ToolCallContext &)>;

}  // namespace mcp
