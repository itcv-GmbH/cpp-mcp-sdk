#pragma once

#include <functional>
#include <optional>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{

struct SamplingCreateMessageContext
{
  jsonrpc::RequestContext requestContext;
};

}  // namespace mcp
