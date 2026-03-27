#pragma once

#include <string>

#include <mcp/export.hpp>

namespace mcp::auth
{

class MCP_SDK_EXPORT AuthVerifier
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
