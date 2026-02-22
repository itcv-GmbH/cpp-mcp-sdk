#pragma once

namespace mcp::jsonrpc
{

/**
 * @brief Options for message serialization.
 */
struct EncodeOptions
{
  /**
   * @brief If true, embedded newlines in string values will be rejected.
   */
  bool disallowEmbeddedNewlines = false;
};

}  // namespace mcp::jsonrpc
