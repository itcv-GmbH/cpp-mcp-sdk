#pragma once

#include <cstdint>

namespace mcp::server
{

enum class ListEndpoint : std::uint8_t
{
  kTools,
  kResources,
  kResourceTemplates,
  kPrompts,
  kTasks,
};

}  // namespace mcp::server
