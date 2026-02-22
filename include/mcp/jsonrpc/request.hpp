#pragma once

#include <optional>
#include <string>

#include <mcp/jsonrpc/types.hpp>
#include <mcp/version.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief JSON-RPC request message.
 */
struct Request
{
  /**
   * @brief JSON-RPC version string.
   * @default "2.0"
   */
  std::string jsonrpc = std::string(kJsonRpcVersion);

  /**
   * @brief Request ID (can be integer or string).
   * @default 0
   */
  RequestId id = std::int64_t {0};

  /**
   * @brief Method name to invoke.
   */
  std::string method;

  /**
   * @brief Optional method parameters.
   */
  std::optional<JsonValue> params;

  /**
   * @brief Additional properties not part of standard JSON-RPC.
   */
  JsonValue additionalProperties = JsonValue::object();
};

}  // namespace mcp::jsonrpc
