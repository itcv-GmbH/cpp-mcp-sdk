#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mcp
{
namespace security
{

struct OriginPolicy
{
  bool validateOrigin = true;
  bool allowRequestsWithoutOrigin = true;
  std::vector<std::string> allowedOrigins;
  std::vector<std::string> allowedHosts = {
    "localhost",
    "127.0.0.1",
    "::1",
    "[::1]",
  };
};

namespace detail
{

struct ParsedOrigin
{
  std::string scheme;
  std::string host;
  std::optional<std::uint16_t> port;
  bool ipv6Literal = false;
};

inline auto toLowerAscii(std::string_view value) -> std::string
{
  std::string normalized;
  normalized.reserve(value.size());

  for (const char character : value)
  {
    normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }

  return normalized;
}

inline auto trimAsciiWhitespace(std::string_view value) -> std::string_view
{
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0)
  {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
  {
    --end;
  }

  return value.substr(begin, end - begin);
}

inline auto isValidScheme(std::string_view scheme) -> bool
{
  if (scheme.empty() || !std::isalpha(static_cast<unsigned char>(scheme.front())))
  {
    return false;
  }

  return std::all_of(scheme.begin(),
                     scheme.end(),
                     [](char character) -> bool
                     {
                       const auto unsignedCharacter = static_cast<unsigned char>(character);
                       return std::isalnum(unsignedCharacter) != 0 || character == '+' || character == '-' || character == '.';
                     });
}

inline auto parsePort(std::string_view portText) -> std::optional<std::uint16_t>
{
  if (portText.empty() || !std::all_of(portText.begin(), portText.end(), [](char character) -> bool { return std::isdigit(static_cast<unsigned char>(character)) != 0; }))
  {
    return std::nullopt;
  }

  std::uint32_t port = 0;
  for (const char character : portText)
  {
    port = (port * 10U) + static_cast<std::uint32_t>(character - '0');
    if (port > 65535U)
    {
      return std::nullopt;
    }
  }

  return static_cast<std::uint16_t>(port);
}

inline auto parseOrigin(std::string_view origin) -> std::optional<ParsedOrigin>
{
  const std::string_view trimmed = trimAsciiWhitespace(origin);
  if (trimmed.empty() || trimmed == "null")
  {
    return std::nullopt;
  }

  const std::size_t schemeSeparator = trimmed.find("://");
  if (schemeSeparator == std::string_view::npos || schemeSeparator == 0)
  {
    return std::nullopt;
  }

  const std::string_view schemeView = trimmed.substr(0, schemeSeparator);
  if (!isValidScheme(schemeView))
  {
    return std::nullopt;
  }

  const std::size_t authorityStart = schemeSeparator + 3;
  std::size_t authorityEnd = trimmed.find_first_of("/?#", authorityStart);
  if (authorityEnd == std::string_view::npos)
  {
    authorityEnd = trimmed.size();
  }

  const std::string_view authority = trimmed.substr(authorityStart, authorityEnd - authorityStart);
  if (authority.empty() || authority.find('@') != std::string_view::npos)
  {
    return std::nullopt;
  }

  ParsedOrigin parsed;
  parsed.scheme = toLowerAscii(schemeView);

  if (authority.front() == '[')
  {
    const std::size_t closingBracket = authority.find(']');
    if (closingBracket == std::string_view::npos || closingBracket <= 1)
    {
      return std::nullopt;
    }

    parsed.ipv6Literal = true;
    parsed.host = toLowerAscii(authority.substr(1, closingBracket - 1));

    const std::string_view remainder = authority.substr(closingBracket + 1);
    if (remainder.empty())
    {
      return parsed;
    }

    if (remainder.front() != ':')
    {
      return std::nullopt;
    }

    parsed.port = parsePort(remainder.substr(1));
    if (!parsed.port.has_value())
    {
      return std::nullopt;
    }

    return parsed;
  }

  const std::size_t portSeparator = authority.rfind(':');
  if (portSeparator == std::string_view::npos)
  {
    parsed.host = toLowerAscii(authority);
    return parsed;
  }

  parsed.host = toLowerAscii(authority.substr(0, portSeparator));
  if (parsed.host.empty())
  {
    return std::nullopt;
  }

  parsed.port = parsePort(authority.substr(portSeparator + 1));
  if (!parsed.port.has_value())
  {
    return std::nullopt;
  }

  return parsed;
}

inline auto normalizeOrigin(const ParsedOrigin &origin) -> std::string
{
  std::string normalized = origin.scheme;
  normalized += "://";

  if (origin.ipv6Literal)
  {
    normalized.push_back('[');
    normalized += origin.host;
    normalized.push_back(']');
  }
  else
  {
    normalized += origin.host;
  }

  if (origin.port.has_value())
  {
    normalized.push_back(':');
    normalized += std::to_string(*origin.port);
  }

  return normalized;
}

inline auto normalizeHost(std::string_view host) -> std::string
{
  const std::string_view trimmed = trimAsciiWhitespace(host);
  if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']')
  {
    return toLowerAscii(trimmed.substr(1, trimmed.size() - 2));
  }

  return toLowerAscii(trimmed);
}

}  // namespace detail

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

  for (const std::string &allowedHost : policy.allowedHosts)
  {
    if (detail::normalizeHost(allowedHost) == parsedOrigin->host)
    {
      return true;
    }
  }

  return false;
}

}  // namespace security
}  // namespace mcp
