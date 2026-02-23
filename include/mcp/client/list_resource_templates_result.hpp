#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/server/resource_template_definition.hpp>

namespace mcp
{

struct ListResourceTemplatesResult
{
  std::vector<server::ResourceTemplateDefinition> resourceTemplates;
  std::optional<std::string> nextCursor;
};

}  // namespace mcp
