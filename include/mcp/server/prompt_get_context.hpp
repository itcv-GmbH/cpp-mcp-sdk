#pragma once

#include <string>

#include <mcp/jsonrpc/all.hpp>

namespace mcp
{

struct PromptGetContext
{
  jsonrpc::RequestContext requestContext;
  std::string promptName;
  jsonrpc::JsonValue arguments = jsonrpc::JsonValue::object();
};

}  // namespace mcp
