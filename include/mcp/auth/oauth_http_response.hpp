#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <mcp/auth/oauth_http_header.hpp>

namespace mcp::auth
{

struct OAuthHttpResponse
{
  std::uint16_t statusCode = 0;
  std::vector<OAuthHttpHeader> headers;
  std::string body;
};

}  // namespace mcp::auth
