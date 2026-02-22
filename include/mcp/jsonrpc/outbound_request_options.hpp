#pragma once

#include <chrono>

#include <mcp/jsonrpc/progress_update.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief Options for outbound requests.
 */
struct OutboundRequestOptions
{
  std::chrono::milliseconds timeout {0};
  bool cancelOnTimeout = true;
  ProgressCallback onProgress;
};

}  // namespace mcp::jsonrpc
