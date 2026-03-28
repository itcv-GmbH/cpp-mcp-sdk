#pragma once

#include <mcp/export.hpp>
#include <stdexcept>

namespace mcp::lifecycle::session
{

/**
 * @brief Exception thrown when a capability check fails.
 */
class MCP_SDK_EXPORT CapabilityError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

}  // namespace mcp::lifecycle::session
