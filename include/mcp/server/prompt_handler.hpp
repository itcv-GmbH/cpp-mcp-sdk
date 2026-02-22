#pragma once

#include <functional>

#include <mcp/server/prompt_get_context.hpp>
#include <mcp/server/prompt_get_result.hpp>

namespace mcp
{

using PromptHandler = std::function<PromptGetResult(const PromptGetContext &)>;

}  // namespace mcp
