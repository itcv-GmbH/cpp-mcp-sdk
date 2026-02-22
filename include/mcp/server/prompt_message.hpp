#pragma once

#include <string>

#include <mcp/jsonrpc/all.hpp>

namespace mcp
{

struct PromptMessage
{
  std::string role;
  jsonrpc::JsonValue content = jsonrpc::JsonValue::object();
};

}  // namespace mcp
