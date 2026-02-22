#pragma once

#include <string>

namespace mcp::auth
{

struct OAuthQueryParameter
{
  std::string name;
  std::string value;
};

}  // namespace mcp::auth
