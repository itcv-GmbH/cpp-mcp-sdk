#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mcp/auth/client_authentication_method.hpp>

namespace mcp::auth
{

struct ResolvedClientIdentity
{
  std::string clientId;
  std::vector<std::string> redirectUris;
  ClientAuthenticationMethod authenticationMethod = ClientAuthenticationMethod::kNone;
  std::optional<std::string> clientSecret;
};

}  // namespace mcp::auth
