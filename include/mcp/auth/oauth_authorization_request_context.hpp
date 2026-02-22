#pragma once

#include <optional>
#include <string>

namespace mcp::auth
{

struct OAuthAuthorizationRequestContext
{
  std::string httpMethod;
  std::string httpPath;
  std::optional<std::string> sessionId;
};

}  // namespace mcp::auth
