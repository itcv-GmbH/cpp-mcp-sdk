#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <mcp/jsonrpc/all.hpp>

namespace mcp::transport::http
{

struct StreamableRequestResult
{
  std::vector<jsonrpc::Message> preResponseMessages;
  std::optional<jsonrpc::Response> response;
  bool useSse = false;
  bool terminateSseAfterResponse = true;
  bool closeSseConnection = false;
  std::optional<std::uint32_t> retryMilliseconds;
};

}  // namespace mcp::transport::http