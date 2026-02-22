#pragma once

#include <cstdint>

namespace mcp::transport::http
{

enum class SessionLookupState : std::uint8_t
{
  kUnknown,
  kActive,
  kExpired,
  kTerminated,
};

}  // namespace mcp::transport::http