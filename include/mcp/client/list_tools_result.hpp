#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/server/tool_definition.hpp>

namespace mcp::client
{

struct ListToolsResult
{
  std::vector<server::ToolDefinition> tools;
  std::optional<std::string> nextCursor;
};

}  // namespace mcp::client
