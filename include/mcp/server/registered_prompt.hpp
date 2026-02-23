#pragma once

#include <mcp/server/prompt_definition.hpp>
#include <mcp/server/prompt_handler.hpp>

namespace mcp::server
{

struct RegisteredPrompt
{
  PromptDefinition definition;
  PromptHandler handler;
};

}  // namespace mcp::server
