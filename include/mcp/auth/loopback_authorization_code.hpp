#pragma once

#include <string>

namespace mcp::auth
{

struct LoopbackAuthorizationCode
{
  std::string code;
  std::string state;
};

}  // namespace mcp::auth
