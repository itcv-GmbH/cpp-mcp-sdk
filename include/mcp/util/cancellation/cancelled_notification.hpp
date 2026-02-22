#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp::util::cancellation
{

struct CancelledNotification
{
  jsonrpc::RequestId requestId;
  std::optional<std::string> reason;
};

}  // namespace mcp::util::cancellation
