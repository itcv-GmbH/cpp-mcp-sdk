#pragma once

#include <cstdint>

namespace mcp
{

enum class ListEndpoint : std::uint8_t
{
  kTools,
  kResources,
  kResourceTemplates,
  kPrompts,
  kTasks,
};

}  // namespace mcp
