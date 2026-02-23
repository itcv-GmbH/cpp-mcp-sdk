#pragma once

#include <optional>
#include <vector>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/server/prompt_message.hpp>

namespace mcp::server
{

struct PromptGetResult
{
  std::optional<std::string> description;
  std::vector<PromptMessage> messages;
  std::optional<jsonrpc::JsonValue> metadata;
};

}  // namespace mcp::server
