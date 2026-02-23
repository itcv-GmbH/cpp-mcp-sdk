#pragma once

/**
 * @file session.hpp
 * @brief Top-level facade header for mcp::Session.
 *
 * This header provides the mcp::Session alias for mcp::lifecycle::Session.
 */

#include <mcp/lifecycle/session.hpp>

namespace mcp
{

/**
 * @brief Top-level alias for mcp::lifecycle::Session.
 */
using Session = lifecycle::Session;

}  // namespace mcp
