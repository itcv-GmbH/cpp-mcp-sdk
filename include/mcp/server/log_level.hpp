#pragma once

#include <cstdint>

namespace mcp::server
{

enum class LogLevel : std::uint8_t
{
  kDebug,
  kInfo,
  kNotice,
  kWarning,
  kError,
  kCritical,
  kAlert,
  kEmergency,
};

}  // namespace mcp::server
