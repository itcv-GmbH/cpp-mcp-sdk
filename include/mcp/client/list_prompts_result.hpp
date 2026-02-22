#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/server/prompts.hpp>

namespace mcp
{

struct ListPromptsResult
{
  std::vector<PromptDefinition> prompts;
  std::optional<std::string> nextCursor;
};

}  // namespace mcp
