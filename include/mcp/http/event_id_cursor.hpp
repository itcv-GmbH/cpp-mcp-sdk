#pragma once

#include <cstdint>
#include <string>

namespace mcp::http::sse
{

struct EventIdCursor
{
  std::string streamId;
  std::uint64_t cursor = 0;
};

}  // namespace mcp::http::sse
