#pragma once

#include <mcp/export.hpp>
#include <stdexcept>

namespace mcp::jsonrpc
{

/**
 * @brief Exception thrown when JSON-RPC message parsing/validation fails.
 *
 * This exception is thrown for:
 * - Invalid JSON syntax
 * - Missing required JSON-RPC fields (jsonrpc, method for requests)
 * - Type mismatches in message structure
 */
class MCP_SDK_EXPORT MessageValidationError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

}  // namespace mcp::jsonrpc
