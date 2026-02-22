#pragma once

#include <string>

namespace mcp::auth
{

struct BearerWwwAuthenticateParameter
{
  std::string name;
  std::string value;
};

}  // namespace mcp::auth
