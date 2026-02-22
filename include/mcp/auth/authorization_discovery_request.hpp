#pragma once

#include <functional>
#include <string>
#include <vector>

#include <mcp/auth/bearer_www_authenticate_challenge.hpp>
#include <mcp/auth/discovery_http_types.hpp>
#include <mcp/auth/discovery_security_policy.hpp>

namespace mcp::auth
{

struct AuthorizationDiscoveryRequest
{
  std::string mcpEndpointUrl;
  std::vector<std::string> wwwAuthenticateHeaderValues;
  DiscoveryHttpFetcher httpFetcher;
  DiscoveryDnsResolver dnsResolver;
  DiscoverySecurityPolicy securityPolicy;
};

}  // namespace mcp::auth
