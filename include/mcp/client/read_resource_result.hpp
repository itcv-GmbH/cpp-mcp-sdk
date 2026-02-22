#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/server/resources.hpp>

namespace mcp
{

struct ReadResourceResult
{
  std::vector<ResourceContent> contents;
};

}  // namespace mcp
