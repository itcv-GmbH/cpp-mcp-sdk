#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/server/tools.hpp>

namespace mcp
{

struct ListToolsResult
{
  std::vector<ToolDefinition> tools;
  std::optional<std::string> nextCursor;
};

}  // namespace mcp
