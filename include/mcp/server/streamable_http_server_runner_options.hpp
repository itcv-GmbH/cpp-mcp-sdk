#pragma once

#include <mcp/transport/all.hpp>

namespace mcp::server
{

struct StreamableHttpServerRunnerOptions
{
  transport::http::StreamableHttpServerOptions transportOptions;
};

}  // namespace mcp::server
