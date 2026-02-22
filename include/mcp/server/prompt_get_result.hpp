#pragma once

#include <optional>
#include <vector>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/server/prompt_message.hpp>

namespace mcp
{

struct PromptGetResult
{
  std::optional<std::string> description;
  std::vector<PromptMessage> messages;
  std::optional<jsonrpc::JsonValue> metadata;
};

}  // namespace mcp
