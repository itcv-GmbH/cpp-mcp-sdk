#pragma once

#include <string>
#include <vector>

namespace mcp::auth
{

struct OAuthScopeSet
{
  std::vector<std::string> values;
};

}  // namespace mcp::auth
