#pragma once

#include <functional>
#include <vector>

#include <mcp/client/root_entry.hpp>
#include <mcp/client/roots_list_context.hpp>

namespace mcp
{

using RootsProvider = std::function<std::vector<RootEntry>(const RootsListContext &)>;

}  // namespace mcp
