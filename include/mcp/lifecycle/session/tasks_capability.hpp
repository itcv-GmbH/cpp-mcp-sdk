#pragma once

namespace mcp
{

/**
 * @brief Tasks capability.
 */
struct TasksCapability
{
  bool list = false;
  bool cancel = false;
  bool samplingCreateMessage = false;
  bool elicitationCreate = false;
  bool toolsCall = false;
};

}  // namespace mcp
