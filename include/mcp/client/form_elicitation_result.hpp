#pragma once

#include <optional>

#include <mcp/client/elicitation_action.hpp>
#include <mcp/jsonrpc/all.hpp>

namespace mcp
{

struct FormElicitationResult
{
  ElicitationAction action = ElicitationAction::kCancel;
  std::optional<jsonrpc::JsonValue> content;
};

}  // namespace mcp
