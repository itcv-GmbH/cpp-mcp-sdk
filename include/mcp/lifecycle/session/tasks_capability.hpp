#pragma once

namespace mcp
{
namespace lifecycle
{
namespace session
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

}  // namespace session
}  // namespace lifecycle
}  // namespace mcp
