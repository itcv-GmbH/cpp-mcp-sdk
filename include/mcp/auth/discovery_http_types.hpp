#pragma once

#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

#include <mcp/auth/discovery_http_request.hpp>
#include <mcp/auth/discovery_http_response.hpp>

namespace mcp::auth
{

using DiscoveryHttpFetcher = std::function<DiscoveryHttpResponse(const DiscoveryHttpRequest &)>;
using DiscoveryDnsResolver = std::function<std::vector<std::string>(std::string_view host)>;

}  // namespace mcp::auth
