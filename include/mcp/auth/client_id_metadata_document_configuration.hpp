#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mcp/auth/client_authentication_method.hpp>

namespace mcp::auth
{

struct ClientIdMetadataDocumentConfiguration
{
  std::string clientId;
  std::string clientName;
  std::vector<std::string> redirectUris;
  ClientAuthenticationMethod authenticationMethod = ClientAuthenticationMethod::kNone;
  std::optional<std::string> clientUri;
  std::optional<std::string> logoUri;
};

}  // namespace mcp::auth
