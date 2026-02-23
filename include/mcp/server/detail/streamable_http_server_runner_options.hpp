#pragma once

#include <mcp/transport/all.hpp>

namespace mcp
{

/// Configuration options for the Streamable HTTP server runner.
struct StreamableHttpServerRunnerOptions
{
  /// Options passed through to the underlying Streamable HTTP transport.
  /// Controls endpoint configuration, TLS, authorization, etc.
  transport::http::StreamableHttpServerOptions transportOptions;
};

}  // namespace mcp
