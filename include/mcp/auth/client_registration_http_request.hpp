#pragma once

#include <string>
#include <vector>

#include <mcp/auth/client_registration_header.hpp>

namespace mcp::auth
{

struct ClientRegistrationHttpRequest
{
  std::string method = "POST";
  std::string url;
  std::vector<ClientRegistrationHeader> headers;
  std::string body;
};

}  // namespace mcp::auth
