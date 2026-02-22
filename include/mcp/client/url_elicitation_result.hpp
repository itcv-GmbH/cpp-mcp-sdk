#pragma once

#include <mcp/client/elicitation_action.hpp>

namespace mcp
{

struct UrlElicitationResult
{
  ElicitationAction action = ElicitationAction::kCancel;
};

}  // namespace mcp
