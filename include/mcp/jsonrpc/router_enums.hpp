#pragma once

#include <cstdint>

namespace mcp::jsonrpc
{

/**
 * @brief Enum to control whether to mark a response ID as ignored.
 */
enum class MarkIgnoredResponseId : std::uint8_t
{
  kNo,
  kYes,
};

/**
 * @brief Result of attempting to activate an inbound request.
 */
enum class InboundRequestActivationResult : std::uint8_t
{
  kAccepted,
  kShuttingDown,
  kDuplicateProgressToken,
  kLimitExceeded,
};

}  // namespace mcp::jsonrpc
