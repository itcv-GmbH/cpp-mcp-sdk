#pragma once

#include <string>

namespace mcp::auth
{

struct ClientRegistrationHeader
{
  std::string name;
  std::string value;
};

}  // namespace mcp::auth
