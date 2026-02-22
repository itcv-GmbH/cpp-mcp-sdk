#pragma once

#include <optional>
#include <string>
#include <vector>

namespace mcp::auth
{

struct AuthorizationServerMetadata
{
  std::string issuer;
  std::optional<std::string> authorizationEndpoint;
  std::optional<std::string> tokenEndpoint;
  std::optional<std::string> registrationEndpoint;
  std::vector<std::string> codeChallengeMethodsSupported;
  bool clientIdMetadataDocumentSupported = false;
};

}  // namespace mcp::auth
