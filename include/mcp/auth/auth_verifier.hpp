#pragma once

#include <string>

namespace mcp::auth
{

class AuthVerifier
{
public:
  virtual ~AuthVerifier() = default;
  virtual auto verify(const std::string &bearerToken) const -> bool = 0;
};

}  // namespace mcp::auth
