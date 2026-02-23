#pragma once

#include <chrono>

namespace mcp::lifecycle::session
{

/**
 * @brief Options for sending a request.
 */
struct RequestOptions
{
  std::chrono::milliseconds timeout {0};
  bool cancelOnTimeout = true;
};

}  // namespace mcp::lifecycle::session
