#pragma once

#include <optional>
#include <string>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{

struct PromptArgumentDefinition
{
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<bool> required;
  std::optional<jsonrpc::JsonValue> metadata;
};

}  // namespace mcp
