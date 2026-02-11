#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{

struct RootEntry
{
  std::string uri;
  std::optional<std::string> name;
  std::optional<jsonrpc::JsonValue> metadata;
};

struct RootsListContext
{
  jsonrpc::RequestContext requestContext;
};

using RootsProvider = std::function<std::vector<RootEntry>(const RootsListContext &)>;

}  // namespace mcp
