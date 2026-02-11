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

using SamplingCreateMessageHandler = std::function<std::optional<jsonrpc::JsonValue>(const SamplingCreateMessageContext &, const jsonrpc::JsonValue &)>;

}  // namespace mcp
