#pragma once

#include <cstdint>

namespace mcp::server
{

enum class ResourceContentKind : std::uint8_t
{
  kText,
  kBlobBase64,
};

}  // namespace mcp::server
