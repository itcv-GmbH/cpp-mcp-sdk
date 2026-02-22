#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace mcp::security
{

inline constexpr std::size_t kDefaultMaxMessageSizeBytes = static_cast<std::size_t>(1024U) * static_cast<std::size_t>(1024U);
inline constexpr std::size_t kDefaultMaxConcurrentInFlightRequests = 1024U;
inline constexpr std::size_t kDefaultMaxSseBufferedMessages = 1024U;
inline constexpr std::int64_t kDefaultMaxSseStreamDurationMilliseconds = 30LL * 60LL * 1000LL;
inline constexpr std::uint32_t kDefaultMaxRetryAttempts = 64U;
inline constexpr std::uint32_t kDefaultMaxRetryDelayMilliseconds = 60U * 1000U;
inline constexpr std::int64_t kDefaultMaxTaskTtlMilliseconds = 24LL * 60LL * 60LL * 1000LL;
inline constexpr std::size_t kDefaultMaxConcurrentTasksPerAuthContext = 128U;

struct RuntimeLimits
{
  std::size_t maxMessageSizeBytes = kDefaultMaxMessageSizeBytes;
  std::size_t maxConcurrentInFlightRequests = kDefaultMaxConcurrentInFlightRequests;
  std::chrono::milliseconds maxSseStreamDuration {kDefaultMaxSseStreamDurationMilliseconds};
  std::size_t maxSseBufferedMessages = kDefaultMaxSseBufferedMessages;
  std::uint32_t maxRetryAttempts = kDefaultMaxRetryAttempts;
  std::uint32_t maxRetryDelayMilliseconds = kDefaultMaxRetryDelayMilliseconds;
  std::int64_t maxTaskTtlMilliseconds = kDefaultMaxTaskTtlMilliseconds;
  std::size_t maxConcurrentTasksPerAuthContext = kDefaultMaxConcurrentTasksPerAuthContext;
};

}  // namespace mcp::security
