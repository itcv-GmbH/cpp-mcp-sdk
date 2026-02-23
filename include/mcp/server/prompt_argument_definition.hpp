#pragma once

#include <optional>
#include <string>

#include <mcp/jsonrpc/all.hpp>

namespace mcp::server
{

struct PromptArgumentDefinition
{
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<bool> required;
  std::optional<jsonrpc::JsonValue> metadata;
};

}  // namespace mcp::server
