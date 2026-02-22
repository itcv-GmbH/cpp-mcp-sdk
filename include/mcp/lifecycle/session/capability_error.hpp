#pragma once

#include <stdexcept>

namespace mcp
{

/**
 * @brief Exception thrown when a capability check fails.
 */
class CapabilityError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

}  // namespace mcp
