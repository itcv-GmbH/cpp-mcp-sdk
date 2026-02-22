#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mcp/auth/oauth_server.hpp>
#include <mcp/transport/http/header.hpp>
#include <mcp/transport/http/header_utils.hpp>
#include <mcp/transport/http/request_kind.hpp>
#include <mcp/transport/http/request_validation_options.hpp>
#include <mcp/transport/http/request_validation_result.hpp>
#include <mcp/transport/http/session_lookup_state.hpp>

namespace mcp::transport::http
{

inline auto rejectRequest(std::uint16_t statusCode, std::string reason) -> RequestValidationResult
{
  RequestValidationResult result;
  result.accepted = false;
  result.statusCode = statusCode;
  result.reason = std::move(reason);
  return result;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
inline auto validateServerRequest(const HeaderList &headers, RequestValidationOptions options = {}) -> RequestValidationResult
{
  if (options.originPolicy.validateOrigin)
  {
    const auto origin = getHeader(headers, kHeaderOrigin);
    if (!origin.has_value() && !options.originPolicy.allowRequestsWithoutOrigin)
    {
      return rejectRequest(detail::kHttpStatusForbidden, "Origin is required");
    }

    if (origin.has_value() && !security::isOriginAllowed(*origin, options.originPolicy))
    {
      return rejectRequest(detail::kHttpStatusForbidden, "Origin is not allowed");
    }
  }

  RequestValidationResult accepted;

  const auto sessionIdHeader = getHeader(headers, kHeaderMcpSessionId);
  if (sessionIdHeader.has_value())
  {
    const std::string_view normalizedSessionId = detail::trimAsciiWhitespace(*sessionIdHeader);
    if (!isValidSessionId(normalizedSessionId))
    {
      return rejectRequest(detail::kHttpStatusBadRequest, "Invalid MCP-Session-Id");
    }

    accepted.sessionId = std::string(normalizedSessionId);
  }
  else if (options.sessionRequired && options.requestKind != RequestKind::kInitialize)
  {
    return rejectRequest(detail::kHttpStatusBadRequest, "Missing required MCP-Session-Id");
  }

  if (accepted.sessionId.has_value() && options.sessionResolver)
  {
    const SessionResolution resolution = options.sessionResolver(*accepted.sessionId);
    if (resolution.state == SessionLookupState::kExpired || resolution.state == SessionLookupState::kTerminated || resolution.state == SessionLookupState::kUnknown)
    {
      return rejectRequest(detail::kHttpStatusNotFound, "Session is not active");
    }

    if (!options.inferredProtocolVersion.has_value() && resolution.negotiatedProtocolVersion.has_value())
    {
      options.inferredProtocolVersion = resolution.negotiatedProtocolVersion;
    }
  }

  const auto headerProtocolVersion = getHeader(headers, kHeaderMcpProtocolVersion);
  if (headerProtocolVersion.has_value())
  {
    const std::string_view normalizedVersion = detail::trimAsciiWhitespace(*headerProtocolVersion);
    if (!isValidProtocolVersion(normalizedVersion))
    {
      return rejectRequest(detail::kHttpStatusBadRequest, "Invalid MCP-Protocol-Version");
    }

    if (!isSupportedProtocolVersion(normalizedVersion, options.supportedProtocolVersions))
    {
      return rejectRequest(detail::kHttpStatusBadRequest, "Unsupported MCP-Protocol-Version");
    }

    accepted.effectiveProtocolVersion = std::string(normalizedVersion);
    return accepted;
  }

  if (options.inferredProtocolVersion.has_value())
  {
    const std::string_view normalizedVersion = detail::trimAsciiWhitespace(*options.inferredProtocolVersion);
    if (!isValidProtocolVersion(normalizedVersion) || !isSupportedProtocolVersion(normalizedVersion, options.supportedProtocolVersions))
    {
      return rejectRequest(detail::kHttpStatusBadRequest, "Inferred protocol version is invalid or unsupported");
    }

    accepted.effectiveProtocolVersion = std::string(normalizedVersion);
    return accepted;
  }

  accepted.effectiveProtocolVersion = std::string(kFallbackProtocolVersion);
  return accepted;
}

}  // namespace mcp::transport::http