#pragma once

#include <chrono>
#include <cstdint>

namespace mcp::transport
{

inline constexpr std::int64_t kDefaultStdioShutdownTimeoutMilliseconds = 1500;

struct StdioSubprocessShutdownOptions
{
  std::chrono::milliseconds waitForExitTimeout {kDefaultStdioShutdownTimeoutMilliseconds};
  std::chrono::milliseconds waitAfterTerminateTimeout {kDefaultStdioShutdownTimeoutMilliseconds};
};

}  // namespace mcp::transport
