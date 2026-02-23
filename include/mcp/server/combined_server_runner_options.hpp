#pragma once

#include <iosfwd>

#include <mcp/server/stdio_server_runner_options.hpp>
#include <mcp/server/streamable_http_server_runner_options.hpp>

namespace mcp::server
{

struct CombinedServerRunnerOptions
{
  StdioServerRunnerOptions stdioOptions;
  StreamableHttpServerRunnerOptions httpOptions;
  bool enableStdio = false;
  bool enableHttp = false;
  std::istream *stdioInput = nullptr;
  std::ostream *stdioOutput = nullptr;
  std::ostream *stdioError = nullptr;
};

}  // namespace mcp::server
