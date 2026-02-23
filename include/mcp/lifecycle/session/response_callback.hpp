#pragma once

#include <functional>

#include <mcp/jsonrpc/router.hpp>



namespace mcp::lifecycle::session
{

/**
 * @brief Callback type for asynchronous response handling.
 */
using ResponseCallback = std::function<void(const jsonrpc::Response &)>;

} // namespace mcp::lifecycle::session


