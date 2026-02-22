#pragma once

#include <variant>

#include <mcp/jsonrpc/error_response.hpp>
#include <mcp/jsonrpc/success_response.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief Response variant type - either success or error.
 */
using Response = std::variant<SuccessResponse, ErrorResponse>;

}  // namespace mcp::jsonrpc
