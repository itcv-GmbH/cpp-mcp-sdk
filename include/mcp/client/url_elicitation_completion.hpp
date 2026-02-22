#pragma once

#include <functional>
#include <string_view>

#include <mcp/client/elicitation_context.hpp>

namespace mcp
{

using UrlElicitationCompletionHandler = std::function<void(const ElicitationCreateContext &, std::string_view)>;

}  // namespace mcp
