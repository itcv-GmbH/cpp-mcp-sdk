#pragma once

#include <string>
#include <vector>

#include <mcp/http/all.hpp>

namespace mcp::transport::http
{

struct SseStreamResponse
{
  std::string streamId;
  std::vector<mcp::http::sse::Event> events;
  bool terminateStream = false;
};

}  // namespace mcp::transport::http