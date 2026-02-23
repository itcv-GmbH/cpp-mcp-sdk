#pragma once

#include <cstdint>

namespace mcp::client
{

enum class ElicitationAction : std::uint8_t
{
  kAccept,
  kDecline,
  kCancel,
};

}  // namespace mcp::client
