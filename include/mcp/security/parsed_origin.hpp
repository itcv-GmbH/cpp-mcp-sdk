#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <mcp/detail/ascii.hpp>

namespace mcp::security::detail
{

inline constexpr std::uint32_t kPortDecimalBase = 10U;
inline constexpr std::uint32_t kMaxTcpPort = 65535U;

using ::mcp::detail::toLowerAscii;
using ::mcp::detail::trimAsciiWhitespace;

struct ParsedOrigin
{
  std::string scheme;
  std::string host;
  std::optional<std::uint16_t> port;
  bool ipv6Literal = false;
};

inline auto isValidScheme(std::string_view scheme) -> bool
{
  if (scheme.empty() || std::isalpha(static_cast<unsigned char>(scheme.front())) == 0)
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
    port = (port * kPortDecimalBase) + static_cast<std::uint32_t>(character - '0');
    if (port > kMaxTcpPort)
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

}  // namespace mcp::security::detail
