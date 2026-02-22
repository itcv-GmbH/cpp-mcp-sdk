#pragma once

#include <string>

#include <mcp/jsonrpc/types.hpp>
#include <mcp/sdk/version.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief JSON-RPC successful response message.
 */
struct SuccessResponse
{
  /**
   * @brief JSON-RPC version string.
   * @default "2.0"
   */
  std::string jsonrpc = std::string(kJsonRpcVersion);

  /**
   * @brief Request ID this response corresponds to.
   */
  RequestId id = std::int64_t {0};

  /**
   * @brief Result of the method invocation.
   */
  JsonValue result;

  /**
   * @brief Additional properties not part of standard JSON-RPC.
   */
  JsonValue additionalProperties = JsonValue::object();
};

}  // namespace mcp::jsonrpc
