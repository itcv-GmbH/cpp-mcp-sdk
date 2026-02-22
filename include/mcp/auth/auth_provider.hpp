#pragma once

#include <future>

#include <mcp/auth/auth_request_context.hpp>
#include <mcp/auth/auth_result.hpp>

namespace mcp::auth
{

class AuthProvider
{
public:
  virtual ~AuthProvider() = default;
  virtual auto authorize(const AuthRequestContext &context) -> std::future<AuthResult> = 0;
};

}  // namespace mcp::auth
