#pragma once

namespace mcp::lifecycle::session
{

/**
 * @brief Sampling capability.
 */
struct SamplingCapability
{
  bool context = false;
  bool tools = false;
};

}  // namespace mcp::lifecycle::session
