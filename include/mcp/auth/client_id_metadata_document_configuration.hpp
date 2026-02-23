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

// Free functions for client ID metadata document handling
auto validateClientIdMetadataDocumentClientIdUrl(std::string_view clientIdUrl) -> void;
auto buildClientIdMetadataDocumentPayload(const ClientIdMetadataDocumentConfiguration &configuration) -> std::string;
auto validateClientIdMetadataDocumentPayload(std::string_view metadataDocumentJson, std::string_view expectedClientIdUrl) -> void;

}  // namespace mcp::auth
