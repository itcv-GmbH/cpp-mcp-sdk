#pragma once

#include <cstdint>
#include <optional>

namespace mcp::util
{

struct TaskAugmentationRequest
{
  bool requested = false;
  bool ttlProvided = false;
  std::optional<std::int64_t> ttl;
};

}  // namespace mcp::util
