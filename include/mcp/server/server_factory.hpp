#pragma once

#include <functional>
#include <memory>

#include <mcp/server/server.hpp>

namespace mcp::server
{

using ServerFactory = std::function<std::shared_ptr<Server>()>;

}  // namespace mcp::server
