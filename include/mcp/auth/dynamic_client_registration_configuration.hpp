#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mcp/auth/client_authentication_method.hpp>

namespace mcp::auth
{

struct DynamicClientRegistrationConfiguration
{
  bool enabled = true;
  std::optional<std::string> clientName;
  std::vector<std::string> redirectUris;
  ClientAuthenticationMethod authenticationMethod = ClientAuthenticationMethod::kNone;
};

}  // namespace mcp::auth
