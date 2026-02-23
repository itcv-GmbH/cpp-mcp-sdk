#pragma once

/**
 * @file server.hpp
 * @brief Top-level facade header for mcp::Server.
 *
 * This header provides the mcp::Server alias for mcp::server::Server.
 */

#include <mcp/server/server.hpp>

namespace mcp
{

/**
 * @brief Top-level alias for mcp::server::Server.
 */
using Server = server::Server;

}  // namespace mcp
