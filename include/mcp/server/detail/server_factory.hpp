#pragma once

#include <functional>
#include <memory>

#include <mcp/server/server.hpp>

namespace mcp
{

/// @brief ServerFactory is a session-agnostic factory function that creates new Server instances.
/// @details Each invocation creates a fresh Server instance with its own Session. This is used by
/// runners that need to create per-session Server instances (e.g., Streamable HTTP server with
/// requireSessionId=true).
using ServerFactory = std::function<std::shared_ptr<Server>()>;

}  // namespace mcp
