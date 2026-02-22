#pragma once

#include <functional>
#include <optional>

#include <mcp/jsonrpc/all.hpp>

namespace mcp
{

struct SamplingCreateMessageContext
{
  jsonrpc::RequestContext requestContext;
};

}  // namespace mcp
