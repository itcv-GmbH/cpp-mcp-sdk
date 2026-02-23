#pragma once

#include <cstdint>
#include <string>

namespace mcp::transport::http
{

struct HttpEndpointConfig
{
  std::string path = "/mcp";
  std::string bindAddress = "127.0.0.1";
  std::uint16_t port = 0;
  bool bindLocalhostOnly = true;
};

}  // namespace mcp::transport::http