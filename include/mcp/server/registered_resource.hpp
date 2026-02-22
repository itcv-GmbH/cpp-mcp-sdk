#pragma once

#include <mcp/server/resource_definition.hpp>
#include <mcp/server/resource_read_handler.hpp>

namespace mcp
{

struct RegisteredResource
{
  ResourceDefinition definition;
  ResourceReadHandler handler;
};

}  // namespace mcp
