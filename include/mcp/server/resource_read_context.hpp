#pragma once

#include <string>

#include <mcp/jsonrpc/all.hpp>

namespace mcp
{

struct ResourceReadContext
{
  jsonrpc::RequestContext requestContext;
  std::string uri;
};

}  // namespace mcp
