#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <mcp/security/origin_policy.hpp>
#include <mcp/transport/http/request_kind.hpp>
#include <mcp/transport/http/session_resolution.hpp>
#include <mcp/version.hpp>

namespace mcp::transport::http
{

using SessionResolver = std::function<SessionResolution(std::string_view sessionId)>;

struct RequestValidationOptions
{
  RequestKind requestKind = RequestKind::kOther;
  bool sessionRequired = false;
  std::vector<std::string> supportedProtocolVersions = {
    std::string(kLatestProtocolVersion),
    std::string(kLegacyProtocolVersion),
    std::string(kFallbackProtocolVersion),
  };
  std::optional<std::string> inferredProtocolVersion;
  security::OriginPolicy originPolicy;
  SessionResolver sessionResolver;
};

}  // namespace mcp::transport::http