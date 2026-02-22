#pragma once

#include <functional>
#include <optional>
#include <string>

#include <mcp/client/elicitation_action.hpp>
#include <mcp/client/elicitation_context.hpp>
#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{

struct FormElicitationRequest
{
  std::string message;
  jsonrpc::JsonValue requestedSchema;
  std::optional<jsonrpc::JsonValue> metadata;
};

struct FormElicitationResult
{
  ElicitationAction action = ElicitationAction::kCancel;
  std::optional<jsonrpc::JsonValue> content;
};

using FormElicitationHandler = std::function<FormElicitationResult(const ElicitationCreateContext &, const FormElicitationRequest &)>;

}  // namespace mcp
