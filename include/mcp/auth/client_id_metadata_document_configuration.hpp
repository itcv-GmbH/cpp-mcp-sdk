#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mcp/auth/client_authentication_method.hpp>
#include <mcp/export.hpp>

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
MCP_SDK_EXPORT auto validateClientIdMetadataDocumentClientIdUrl(std::string_view clientIdUrl) -> void;
MCP_SDK_EXPORT auto buildClientIdMetadataDocumentPayload(const ClientIdMetadataDocumentConfiguration &configuration) -> std::string;
MCP_SDK_EXPORT auto validateClientIdMetadataDocumentPayload(std::string_view metadataDocumentJson, std::string_view expectedClientIdUrl) -> void;

}  // namespace mcp::auth
