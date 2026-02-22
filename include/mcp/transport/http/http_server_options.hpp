#pragma once

#include <optional>
#include <string>
#include <vector>

#include <mcp/auth/oauth_server.hpp>
#include <mcp/error_reporter.hpp>
#include <mcp/security/limits.hpp>
#include <mcp/security/origin_policy.hpp>
#include <mcp/transport/http/http_endpoint_config.hpp>
#include <mcp/transport/http/server_tls_configuration.hpp>
#include <mcp/version.hpp>

namespace mcp::transport
{

struct HttpServerOptions
{
  HttpEndpointConfig endpoint;
  std::optional<http::ServerTlsConfiguration> tls;
  security::OriginPolicy originPolicy;
  security::RuntimeLimits limits;
  std::optional<auth::OAuthServerAuthorizationOptions> authorization;
  bool requireSessionId = false;
  std::vector<std::string> supportedProtocolVersions = {
    std::string(kLatestProtocolVersion),
    std::string(kLegacyProtocolVersion),
    std::string(kFallbackProtocolVersion),
  };
  /// Error reporter callback for background execution context failures.
  /// If not set, errors are silently suppressed.
  ErrorReporter errorReporter;
};

}  // namespace mcp::transport