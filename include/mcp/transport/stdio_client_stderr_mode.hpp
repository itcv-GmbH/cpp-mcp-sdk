#pragma once

#include <cstdint>

namespace mcp::transport
{

enum class StdioClientStderrMode : std::uint8_t
{
  kCapture,
  kForward,
  kIgnore,
};

}  // namespace mcp::transport
