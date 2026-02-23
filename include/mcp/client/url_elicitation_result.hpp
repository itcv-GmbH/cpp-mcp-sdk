#pragma once

#include <mcp/client/elicitation_action.hpp>

namespace mcp::client
{

struct UrlElicitationResult
{
  ElicitationAction action = ElicitationAction::kCancel;
};

}  // namespace mcp::client
