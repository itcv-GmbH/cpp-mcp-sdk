#pragma once

#include <cstddef>

#include <mcp/sdk/error_reporter.hpp>
#include <mcp/security/limits.hpp>

namespace mcp::jsonrpc
{

/**
 * @brief Configuration options for the Router.
 */
struct RouterOptions
{
  std::size_t maxConcurrentInFlightRequests = security::kDefaultMaxConcurrentInFlightRequests;
  /// Error reporter callback for background execution context failures.
  /// If not set, errors are silently suppressed.
  ::mcp::ErrorReporter errorReporter;
};

}  // namespace mcp::jsonrpc
