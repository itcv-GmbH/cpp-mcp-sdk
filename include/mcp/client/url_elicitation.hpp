#pragma once

#include <functional>
#include <optional>
#include <string>

#include <mcp/client/elicitation_action.hpp>
#include <mcp/client/elicitation_context.hpp>
#include <mcp/jsonrpc/messages.hpp>

namespace mcp
{

struct UrlElicitationDisplayInfo
{
  std::string fullUrl;
  std::string domain;
};

struct UrlElicitationRequest
{
  std::string elicitationId;
  std::string message;
  std::string url;
  std::optional<jsonrpc::JsonValue> metadata;
};

struct UrlElicitationResult
{
  ElicitationAction action = ElicitationAction::kCancel;
};

using UrlElicitationHandler = std::function<UrlElicitationResult(const ElicitationCreateContext &, const UrlElicitationRequest &)>;

}  // namespace mcp
