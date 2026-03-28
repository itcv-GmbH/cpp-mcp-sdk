#pragma once

#include <optional>

#include <mcp/export.hpp>
#include <mcp/jsonrpc/error_response.hpp>
#include <mcp/jsonrpc/success_response.hpp>
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
MCP_SDK_EXPORT auto makeErrorResponse(JsonRpcError error, std::optional<RequestId> id = std::nullopt) -> ErrorResponse;

/**
 * @brief Create an ErrorResponse for a request with an unknown ID.
 *
 * @param error The error to wrap in a response
 * @return ErrorResponse structure with hasUnknownId set to true
 */
MCP_SDK_EXPORT auto makeUnknownIdErrorResponse(JsonRpcError error) -> ErrorResponse;

/**
 * @brief Create a success response with the given ID and result object.
 *
 * @param requestId The request ID to echo
 * @param result The result object (will be moved)
 * @return SuccessResponse with id and result set
 */
MCP_SDK_EXPORT auto makeSuccessResponse(const RequestId &requestId, JsonValue result) -> SuccessResponse;

}  // namespace mcp::jsonrpc
