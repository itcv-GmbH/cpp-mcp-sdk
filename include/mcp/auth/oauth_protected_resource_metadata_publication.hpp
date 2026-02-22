#pragma once

#include <optional>
#include <string>

namespace mcp::auth
{

struct OAuthProtectedResourceMetadataPublication
{
  bool publishAtPathBasedWellKnownUri = true;
  bool publishAtRootWellKnownUri = true;
  std::optional<std::string> challengeResourceMetadataUrl;
};

}  // namespace mcp::auth
