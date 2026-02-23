#pragma once

#include <functional>
#include <vector>

#include <mcp/server/resource_content.hpp>
#include <mcp/server/resource_read_context.hpp>

namespace mcp::server
{

using ResourceReadHandler = std::function<std::vector<ResourceContent>(const ResourceReadContext &)>;

}  // namespace mcp::server
