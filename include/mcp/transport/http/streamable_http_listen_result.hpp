#pragma once

#include <cstdint>
#include <vector>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp::transport::http
{

struct StreamableHttpListenResult
{
  std::uint16_t statusCode = 0;
  std::vector<jsonrpc::Message> messages;
  bool streamOpen = false;
};

}  // namespace mcp::transport::http