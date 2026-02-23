#pragma once

namespace mcp::lifecycle::session
{

/**
 * @brief Resources capability.
 */
struct ResourcesCapability
{
  bool subscribe = false;
  bool listChanged = false;
};

}  // namespace mcp::lifecycle::session
