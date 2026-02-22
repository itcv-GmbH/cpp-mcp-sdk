#pragma once

#include <string>
#include <vector>

#include <mcp/auth/oauth_http_header.hpp>

namespace mcp::auth
{

struct OAuthProtectedResourceRequest
{
  std::string method = "POST";
  std::string url;
  std::vector<OAuthHttpHeader> headers;
  std::string body;
};

}  // namespace mcp::auth
