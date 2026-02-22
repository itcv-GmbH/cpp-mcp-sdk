#pragma once

#include <cstdint>

namespace mcp
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

}  // namespace mcp
