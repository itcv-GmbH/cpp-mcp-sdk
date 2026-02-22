#pragma once

#include <cstdint>

namespace mcp
{

enum class ResourceContentKind : std::uint8_t
{
  kText,
  kBlobBase64,
};

}  // namespace mcp
