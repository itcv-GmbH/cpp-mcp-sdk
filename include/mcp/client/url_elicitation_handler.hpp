#pragma once

#include <functional>

#include <mcp/client/elicitation_context.hpp>
#include <mcp/client/url_elicitation_request.hpp>
#include <mcp/client/url_elicitation_result.hpp>

namespace mcp
{

using UrlElicitationHandler = std::function<UrlElicitationResult(const ElicitationCreateContext &, const UrlElicitationRequest &)>;

}  // namespace mcp
