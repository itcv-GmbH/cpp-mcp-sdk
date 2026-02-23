#pragma once

#include <cstdint>

namespace mcp
{
namespace lifecycle
{
namespace session
{

/**
 * @brief Role of a session participant.
 */
enum class SessionRole : std::uint8_t
{
  kClient,
  kServer,
};

}  // namespace session
}  // namespace lifecycle
}  // namespace mcp
