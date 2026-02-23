#pragma once

#include <stdexcept>



namespace mcp::lifecycle::session
{

/**
 * @brief Exception thrown when a capability check fails.
 */
class CapabilityError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

} // namespace mcp::lifecycle::session


