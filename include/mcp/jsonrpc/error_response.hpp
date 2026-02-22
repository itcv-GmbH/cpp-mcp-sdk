#pragma once

#include <optional>
#include <string>

#include <mcp/errors.hpp>
#include <mcp/jsonrpc/types.hpp>
#include <mcp/version.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief JSON-RPC error response message.
 */
struct ErrorResponse
{
  /**
   * @brief JSON-RPC version string.
   * @default "2.0"
   */
  std::string jsonrpc = std::string(kJsonRpcVersion);

  /**
   * @brief Request ID this error response corresponds to (nullopt for parse errors).
   */
  std::optional<RequestId> id;

  /**
   * @brief True if the request ID was unknown/invalid.
   */
  bool hasUnknownId = false;

  /**
   * @brief Error details.
   */
  JsonRpcError error;

  /**
   * @brief Additional properties not part of standard JSON-RPC.
   */
  JsonValue additionalProperties = JsonValue::object();
};

}  // namespace mcp::jsonrpc
