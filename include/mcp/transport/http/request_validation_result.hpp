#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <mcp/auth/oauth_server.hpp>

namespace mcp::transport::http
{

struct RequestValidationResult
{
  bool accepted = true;
  std::uint16_t statusCode = detail::kHttpStatusOk;
  std::string reason;
  std::optional<std::string> sessionId;
  std::string effectiveProtocolVersion;
  std::optional<auth::OAuthAuthorizationContext> authorizationContext;
};

}  // namespace mcp::transport::http