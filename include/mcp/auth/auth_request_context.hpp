#pragma once

#include <optional>
#include <string>

namespace mcp::auth
{

struct AuthRequestContext
{
  std::string httpMethod;
  std::string endpoint;
  std::optional<std::string> sessionId;
};

}  // namespace mcp::auth
