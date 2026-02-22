#pragma once

#include <optional>
#include <string>

#include <jsoncons/json.hpp>
#include <mcp/sdk/errors.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief Create a JsonRpcError with the specified code, message, and optional data.
 *
 * @param code JSON-RPC error code
 * @param message Human-readable error message
 * @param data Optional additional error data
 * @return JsonRpcError structure
 */
auto makeJsonRpcError(JsonRpcErrorCode code, std::string message, std::optional<jsoncons::json> data = std::nullopt) -> JsonRpcError;

/**
 * @brief Create a parse error (-32700).
 *
 * @param data Optional additional error data
 * @param message Error message (default: "Parse error")
 * @return JsonRpcError structure
 */
auto makeParseError(std::optional<jsoncons::json> data = std::nullopt, std::string message = "Parse error") -> JsonRpcError;

/**
 * @brief Create an invalid request error (-32600).
 *
 * @param data Optional additional error data
 * @param message Error message (default: "Invalid Request")
 * @return JsonRpcError structure
 */
auto makeInvalidRequestError(std::optional<jsoncons::json> data = std::nullopt, std::string message = "Invalid Request") -> JsonRpcError;

/**
 * @brief Create a method not found error (-32601).
 *
 * @param data Optional additional error data
 * @param message Error message (default: "Method not found")
 * @return JsonRpcError structure
 */
auto makeMethodNotFoundError(std::optional<jsoncons::json> data = std::nullopt, std::string message = "Method not found") -> JsonRpcError;

/**
 * @brief Create an invalid params error (-32602).
 *
 * @param data Optional additional error data
 * @param message Error message (default: "Invalid params")
 * @return JsonRpcError structure
 */
auto makeInvalidParamsError(std::optional<jsoncons::json> data = std::nullopt, std::string message = "Invalid params") -> JsonRpcError;

/**
 * @brief Create an internal error (-32603).
 *
 * @param data Optional additional error data
 * @param message Error message (default: "Internal error")
 * @return JsonRpcError structure
 */
auto makeInternalError(std::optional<jsoncons::json> data = std::nullopt, std::string message = "Internal error") -> JsonRpcError;

/**
 * @brief Create a URL elicitation required error (-32042).
 *
 * @param data Optional additional error data
 * @param message Error message (default: "URL elicitation required")
 * @return JsonRpcError structure
 */
auto makeUrlElicitationRequiredError(std::optional<jsoncons::json> data = std::nullopt, std::string message = "URL elicitation required") -> JsonRpcError;

}  // namespace mcp::jsonrpc
