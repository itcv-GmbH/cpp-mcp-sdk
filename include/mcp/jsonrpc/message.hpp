#pragma once

#include <variant>

#include <mcp/jsonrpc/error_response.hpp>
#include <mcp/jsonrpc/notification.hpp>
#include <mcp/jsonrpc/request.hpp>
#include <mcp/jsonrpc/success_response.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief Message variant type - can be any JSON-RPC message type.
 */
using Message = std::variant<Request, Notification, SuccessResponse, ErrorResponse>;

}  // namespace mcp::jsonrpc
