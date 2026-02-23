#pragma once

#include <functional>
#include <string_view>

#include <mcp/client/elicitation_context.hpp>

namespace mcp::client
{

using UrlElicitationCompletionHandler = std::function<void(const ElicitationCreateContext &, std::string_view)>;

}  // namespace mcp::client
