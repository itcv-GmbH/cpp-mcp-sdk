#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <mcp/auth/client_registration_header.hpp>

namespace mcp::auth
{

struct ClientRegistrationHttpResponse
{
  std::uint16_t statusCode = 0;
  std::vector<ClientRegistrationHeader> headers;
  std::string body;
};

}  // namespace mcp::auth
