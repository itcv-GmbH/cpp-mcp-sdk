#pragma once

#include <future>
#include <optional>
#include <string>
#include <vector>

namespace mcp
{
namespace auth
{

struct AuthRequestContext
{
  std::string httpMethod;
  std::string endpoint;
  std::optional<std::string> sessionId;
};

struct AuthResult
{
  std::optional<std::string> bearerToken;
  std::vector<std::string> scopes;
};

class AuthProvider
{
public:
  virtual ~AuthProvider() = default;
  virtual auto authorize(const AuthRequestContext &context) -> std::future<AuthResult> = 0;
};

class AuthVerifier
{
public:
  virtual ~AuthVerifier() = default;
  virtual auto verify(const std::string &bearerToken) const -> bool = 0;
};

}  // namespace auth
}  // namespace mcp
