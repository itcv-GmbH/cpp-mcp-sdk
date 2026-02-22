#pragma once

#include <optional>
#include <string>

#include <mcp/version.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief Context for processing a JSON-RPC request.
 */
struct RequestContext
{
  /**
   * @brief The MCP protocol version to use for this request.
   * @default kLatestProtocolVersion
   */
  std::string protocolVersion = std::string(kLatestProtocolVersion);

  /**
   * @brief Optional session ID for the request.
   */
  std::optional<std::string> sessionId;

  /**
   * @brief Optional authentication context for the request.
   */
  std::optional<std::string> authContext;
};

}  // namespace mcp::jsonrpc
