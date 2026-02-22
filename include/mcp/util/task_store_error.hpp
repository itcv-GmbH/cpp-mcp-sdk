#pragma once

#include <cstdint>

namespace mcp::util
{

enum class TaskStoreError : std::uint8_t
{
  kNone,
  kNotFound,
  kAccessDenied,
  kInvalidTransition,
  kTerminalImmutable,
  kLimitExceeded,
};

}  // namespace mcp::util
