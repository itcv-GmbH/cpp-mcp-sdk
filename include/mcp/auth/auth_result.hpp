#pragma once

#include <optional>
#include <string>
#include <vector>

namespace mcp::auth
{

struct AuthResult
{
  std::optional<std::string> bearerToken;
  std::vector<std::string> scopes;
};

}  // namespace mcp::auth
