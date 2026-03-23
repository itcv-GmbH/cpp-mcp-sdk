#pragma once

#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>

#include <mcp/security/detail/parsed_origin.hpp>
#include <mcp/security/origin_policy_config.hpp>

namespace mcp::security
{

inline auto isOriginAllowed(std::string_view origin, const OriginPolicy &policy = {}) -> bool
{
  if (!policy.validateOrigin)
  {
    return true;
  }

  const std::string_view trimmedOrigin = detail::trimAsciiWhitespace(origin);
  if (trimmedOrigin.empty())
  {
    return false;
  }

  const std::string lowerRawOrigin = detail::toLowerAscii(trimmedOrigin);
  for (const std::string &allowedOrigin : policy.allowedOrigins)
  {
    if (detail::toLowerAscii(detail::trimAsciiWhitespace(allowedOrigin)) == lowerRawOrigin)
    {
      return true;
    }
  }

  const auto parsedOrigin = detail::parseOrigin(trimmedOrigin);
  if (!parsedOrigin.has_value())
  {
    return false;
  }

  const std::string normalizedOrigin = detail::normalizeOrigin(*parsedOrigin);
  for (const std::string &allowedOrigin : policy.allowedOrigins)
  {
    const auto parsedAllowedOrigin = detail::parseOrigin(allowedOrigin);
    if (parsedAllowedOrigin.has_value() && detail::normalizeOrigin(*parsedAllowedOrigin) == normalizedOrigin)
    {
      return true;
    }
  }

  return std::ranges::any_of(policy.allowedHosts,
                             [parsedHost = parsedOrigin->host](const std::string &allowedHost) -> bool { return detail::normalizeHost(allowedHost) == parsedHost; });
}

}  // namespace mcp::security
