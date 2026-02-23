#pragma once

#include <optional>
#include <string>

#include <mcp/jsonrpc/all.hpp>

namespace mcp::client
{

struct RootEntry
{
  std::string uri;
  std::optional<std::string> name;
  std::optional<jsonrpc::JsonValue> metadata;
};

}  // namespace mcp::client
