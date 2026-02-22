#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <mcp/jsonrpc/all.hpp>

namespace mcp::transport::http
{

struct StreamableHttpSendResult
{
  std::uint16_t statusCode = 0;
  std::vector<jsonrpc::Message> messages;
  std::optional<jsonrpc::Response> response;
};

}  // namespace mcp::transport::http