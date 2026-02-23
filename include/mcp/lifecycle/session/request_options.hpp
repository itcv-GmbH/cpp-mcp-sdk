#pragma once

#include <chrono>

namespace mcp
{
namespace lifecycle
{
namespace session
{

/**
 * @brief Options for sending a request.
 */
struct RequestOptions
{
  std::chrono::milliseconds timeout {0};
  bool cancelOnTimeout = true;
};

}  // namespace session
}  // namespace lifecycle
}  // namespace mcp
