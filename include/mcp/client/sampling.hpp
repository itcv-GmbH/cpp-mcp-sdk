#pragma once

#include <functional>
#include <optional>

#include <mcp/client/sampling_create_message_context.hpp>
#include <mcp/jsonrpc/all.hpp>

namespace mcp::client
{

using SamplingCreateMessageHandler = std::function<std::optional<jsonrpc::JsonValue>(const SamplingCreateMessageContext &, const jsonrpc::JsonValue &)>;

}  // namespace mcp::client
