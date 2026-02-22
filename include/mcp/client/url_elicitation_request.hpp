#pragma once

#include <optional>
#include <string>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{

struct UrlElicitationRequest
{
  std::string elicitationId;
  std::string message;
  std::string url;
  std::optional<jsonrpc::JsonValue> metadata;
};

}  // namespace mcp
