#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <mcp/transport/http/http_server_options.hpp>

namespace mcp::transport::http
{

struct StreamableHttpServerOptions
{
  HttpServerOptions http;
  bool allowGetSse = true;
  bool allowDeleteSession = true;
  std::optional<std::uint32_t> defaultSseRetryMilliseconds;
  std::optional<bool> enableLegacyHttpSseCompatibility;
  std::string legacyPostEndpointPath = "/rpc";
  std::string legacySseEndpointPath = "/events";
};

}  // namespace mcp::transport::http