#pragma once

#include <mcp/server/prompt_definition.hpp>
#include <mcp/server/prompt_handler.hpp>

namespace mcp
{

struct RegisteredPrompt
{
  PromptDefinition definition;
  PromptHandler handler;
};

}  // namespace mcp
