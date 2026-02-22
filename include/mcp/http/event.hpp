#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace mcp::http::sse
{

struct Event
{
  std::optional<std::string> event;
  std::optional<std::string> id;
  std::optional<std::uint32_t> retryMilliseconds;
  std::string data;
};

}  // namespace mcp::http::sse
