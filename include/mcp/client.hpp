#pragma once

/**
 * @file client.hpp
 * @brief Top-level facade header for mcp::Client.
 *
 * This header provides the mcp::Client alias for mcp::client::Client.
 */

#include <mcp/client/client.hpp>

namespace mcp
{

/**
 * @brief Top-level alias for mcp::client::Client.
 */
using Client = client::Client;

}  // namespace mcp
