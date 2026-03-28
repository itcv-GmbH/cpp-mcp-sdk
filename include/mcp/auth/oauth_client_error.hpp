#pragma once

#include <stdexcept>
#include <string>

#include <mcp/auth/oauth_client_error_code.hpp>
#include <mcp/export.hpp>

namespace mcp::auth
{

class MCP_SDK_EXPORT OAuthClientError : public std::runtime_error
{
public:
  OAuthClientError(OAuthClientErrorCode code, const std::string &message);

  [[nodiscard]] auto code() const noexcept -> OAuthClientErrorCode;

private:
  OAuthClientErrorCode code_;
};

}  // namespace mcp::auth
