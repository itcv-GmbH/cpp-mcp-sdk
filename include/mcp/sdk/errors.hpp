#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <jsoncons/json.hpp>

namespace mcp
{
namespace sdk
{
/**
 * @brief MCP SDK error codes and structures.
 *
 * @section Exceptions
 *
 * @subsection JSON-RPC Error Codes
 * These error codes map between MCP protocol errors and JSON-RPC responses:
 * - kParseError (-32700): Invalid JSON received
 * - kInvalidRequest (-32600): Request object is malformed
 * - kMethodNotFound (-32601): Method does not exist
 * - kInvalidParams (-32602): Invalid method parameters
 * - kInternalError (-32603): Internal JSON-RPC error
 * - kResourceNotFound (-32002): Resource not found (MCP-specific)
 * - kUrlElicitationRequired (-32042): URL elicitation required (MCP-specific)
 *
 * @subsection Error Code vs C++ Exception
 * - Protocol errors (kMethodNotFound, kInvalidParams) are returned as ErrorResponse objects
 * - Parse errors (kParseError) are thrown as MessageValidationError
 * - System errors (transport failures) are thrown as std::runtime_error
 *
 * @subsection JsonRpcError Structure
 * The JsonRpcError struct is used for JSON-RPC error responses:
 * - code: One of the JsonRpcErrorCode values or application-defined
 * - message: Human-readable error description
 * - data: Optional additional error data (jsoncons::json)
 *
 * This structure is noexcept-copyable and noexcept-movable.
 */
enum class JsonRpcErrorCode : std::int16_t
{
  kParseError = -32700,
  kInvalidRequest = -32600,
  kMethodNotFound = -32601,
  kInvalidParams = -32602,
  kInternalError = -32603,
  kResourceNotFound = -32002,
  kUrlElicitationRequired = -32042,
};

struct JsonRpcError
{
  std::int32_t code = static_cast<std::int32_t>(JsonRpcErrorCode::kInternalError);
  std::string message;
  std::optional<jsoncons::json> data;
};

}  // namespace sdk

// Deprecated: Backwards compatibility aliases
using sdk::JsonRpcError;
using sdk::JsonRpcErrorCode;

}  // namespace mcp
