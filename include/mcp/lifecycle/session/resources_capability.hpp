#pragma once

namespace mcp
{
namespace lifecycle
{
namespace session
{

/**
 * @brief Resources capability.
 */
struct ResourcesCapability
{
  bool subscribe = false;
  bool listChanged = false;
};

}  // namespace session
}  // namespace lifecycle
}  // namespace mcp
