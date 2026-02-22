#pragma once

#include <cstdint>

namespace mcp
{

/**
 * @brief Role of a session participant.
 */
enum class SessionRole : std::uint8_t
{
  kClient,
  kServer,
};

}  // namespace mcp
