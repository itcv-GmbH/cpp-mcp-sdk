#pragma once

#include <cstdint>

namespace mcp::lifecycle::session
{

/**
 * @brief Role of a session participant.
 */
enum class SessionRole : std::uint8_t
{
  kClient,
  kServer,
};

}  // namespace mcp::lifecycle::session
