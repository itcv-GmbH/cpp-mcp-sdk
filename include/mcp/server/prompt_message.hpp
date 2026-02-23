#pragma once

#include <string>

#include <mcp/jsonrpc/all.hpp>

namespace mcp::server
{

struct PromptMessage
{
  std::string role;
  jsonrpc::JsonValue content = jsonrpc::JsonValue::object();
};

}  // namespace mcp::server
