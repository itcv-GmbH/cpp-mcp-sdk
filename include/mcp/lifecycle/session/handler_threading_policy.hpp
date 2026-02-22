#pragma once

#include <cstdint>

namespace mcp
{

/**
 * @brief Policy for handler threading behavior.
 */
enum class HandlerThreadingPolicy : std::uint8_t
{
  kIoThread,  ///< Handlers are invoked directly on the I/O thread.
  kExecutor,  ///< Handlers are dispatched to the configured Executor.
};

}  // namespace mcp
