#pragma once

#include <stdexcept>

namespace mcp::lifecycle::session
{

/**
 * @brief Exception thrown on invalid session state transitions or operations in wrong state.
 */
class LifecycleError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

}  // namespace mcp::lifecycle::session
