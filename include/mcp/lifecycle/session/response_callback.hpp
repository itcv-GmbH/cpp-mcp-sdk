#pragma once

#include <functional>

#include <mcp/jsonrpc/router.hpp>

namespace mcp
{
namespace lifecycle
{
namespace session
{

/**
 * @brief Callback type for asynchronous response handling.
 */
using ResponseCallback = std::function<void(const jsonrpc::Response &)>;

}  // namespace session
}  // namespace lifecycle
}  // namespace mcp
