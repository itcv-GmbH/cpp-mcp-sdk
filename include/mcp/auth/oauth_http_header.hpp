#pragma once

#include <string>

namespace mcp::auth
{

struct OAuthHttpHeader
{
  std::string name;
  std::string value;
};

}  // namespace mcp::auth
