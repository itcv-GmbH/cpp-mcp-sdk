#pragma once

#include <optional>

#include <mcp/jsonrpc/error_response.hpp>
#include <mcp/jsonrpc/types.hpp>
#include <mcp/sdk/errors.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief Create an ErrorResponse from a JsonRpcError.
 *
 * @param error The error to wrap in a response
 * @param id Optional request ID (nullopt for parse errors where ID is unknown)
 * @return ErrorResponse structure
 */
auto makeErrorResponse(JsonRpcError error, std::optional<RequestId> id = std::nullopt) -> ErrorResponse;

/**
 * @brief Create an ErrorResponse for a request with an unknown ID.
 *
 * @param error The error to wrap in a response
 * @return ErrorResponse structure with hasUnknownId set to true
 */
auto makeUnknownIdErrorResponse(JsonRpcError error) -> ErrorResponse;

}  // namespace mcp::jsonrpc
