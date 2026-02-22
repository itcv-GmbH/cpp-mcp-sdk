#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/server/resources.hpp>

namespace mcp
{

struct ListResourceTemplatesResult
{
  std::vector<ResourceTemplateDefinition> resourceTemplates;
  std::optional<std::string> nextCursor;
};

}  // namespace mcp
