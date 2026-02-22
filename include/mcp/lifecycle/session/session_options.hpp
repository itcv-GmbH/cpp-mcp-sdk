#pragma once

#include <string>
#include <vector>

#include <mcp/lifecycle/session/session_threading.hpp>
#include <mcp/sdk/error_reporter.hpp>
#include <mcp/sdk/version.hpp>

namespace mcp
{

/**
 * @brief Configuration options for a Session.
 */
struct SessionOptions
{
  SessionThreading threading;
  std::vector<std::string> supportedProtocolVersions = {
    std::string(kLatestProtocolVersion),
    std::string(kLegacyProtocolVersion),
  };
  /// Error reporter callback for background execution context failures.
  /// If not set, errors are silently suppressed.
  ErrorReporter errorReporter;
};

}  // namespace mcp
