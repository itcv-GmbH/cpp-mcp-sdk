#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include <mcp/jsonrpc/all.hpp>

namespace mcp::util::progress
{

struct ProgressNotification
{
  jsonrpc::RequestId progressToken;
  double progress = 0.0;
  std::optional<double> total;
  std::optional<std::string> message;
  jsonrpc::JsonValue additionalProperties = jsonrpc::JsonValue::object();
};

}  // namespace mcp::util::progress
