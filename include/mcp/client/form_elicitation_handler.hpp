#pragma once

#include <functional>

#include <mcp/client/elicitation_context.hpp>
#include <mcp/client/form_elicitation_request.hpp>
#include <mcp/client/form_elicitation_result.hpp>

namespace mcp::client
{

using FormElicitationHandler = std::function<FormElicitationResult(const ElicitationCreateContext &, const FormElicitationRequest &)>;

}  // namespace mcp::client
