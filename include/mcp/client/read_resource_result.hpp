#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/server/resource_content.hpp>

namespace mcp
{

struct ReadResourceResult
{
  std::vector<server::ResourceContent> contents;
};

}  // namespace mcp
