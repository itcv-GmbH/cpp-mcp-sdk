#pragma once

#include <cstddef>
#include <cstdint>

#include <mcp/security/limits.hpp>

namespace mcp::util
{

struct InMemoryTaskStoreOptions
{
  std::int64_t maxTaskTtlMilliseconds = static_cast<std::int64_t>(security::kDefaultMaxTaskTtlMilliseconds);
  std::size_t maxActiveTasksPerAuthContext = security::kDefaultMaxConcurrentTasksPerAuthContext;
};

}  // namespace mcp::util
