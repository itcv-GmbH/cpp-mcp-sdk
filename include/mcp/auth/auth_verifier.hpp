#pragma once

#include <string>

namespace mcp::auth
{

class AuthVerifier
{
public:
  AuthVerifier() = default;
  AuthVerifier(const AuthVerifier &) = default;
  AuthVerifier(AuthVerifier &&) noexcept = default;
  auto operator=(const AuthVerifier &) -> AuthVerifier & = default;
  auto operator=(AuthVerifier &&) noexcept -> AuthVerifier & = default;
  virtual ~AuthVerifier() = default;
  virtual auto verify(const std::string &bearerToken) const -> bool = 0;
};

}  // namespace mcp::auth
