#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mcp/jsonrpc/all.hpp>
#include <mcp/server/prompt_argument_definition.hpp>

namespace mcp
{

struct PromptDefinition
{
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<jsonrpc::JsonValue> icons;
  std::vector<PromptArgumentDefinition> arguments;
  std::optional<jsonrpc::JsonValue> metadata;
};

}  // namespace mcp
