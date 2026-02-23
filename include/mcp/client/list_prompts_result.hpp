#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/server/prompt_definition.hpp>

namespace mcp
{

struct ListPromptsResult
{
  std::vector<server::PromptDefinition> prompts;
  std::optional<std::string> nextCursor;
};

}  // namespace mcp
