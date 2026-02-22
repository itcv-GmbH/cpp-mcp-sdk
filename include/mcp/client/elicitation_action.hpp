#pragma once

#include <cstdint>

namespace mcp
{

enum class ElicitationAction : std::uint8_t
{
  kAccept,
  kDecline,
  kCancel,
};

}  // namespace mcp
