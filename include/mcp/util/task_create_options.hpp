#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace mcp::util
{

struct TaskCreateOptions
{
  std::optional<std::int64_t> ttl;
  std::optional<std::int64_t> pollInterval;
  std::optional<std::string> statusMessage;
  std::optional<std::string> authContext;
};

}  // namespace mcp::util
