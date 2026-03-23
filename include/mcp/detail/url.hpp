#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <mcp/detail/parsed_absolute_url.hpp>

namespace mcp::detail
{

constexpr std::uint8_t kAsciiControlMax = 0x20;
constexpr char kAsciiDelete = 0x7F;
constexpr std::uint32_t kMaxPort = 65535;
constexpr std::uint32_t kDecimalBase = 10;
constexpr std::uint16_t kPortHttp = 80;
constexpr std::uint16_t kPortHttps = 443;
constexpr std::uint16_t kPortFtp = 21;
constexpr std::uint16_t kPortFtps = 990;

namespace detail
{

/**
 * Check if a character is an ASCII whitespace or control character.
 */
inline auto isWhitespaceOrControl(char c) -> bool
{
  return static_cast<unsigned char>(c) <= kAsciiControlMax || c == kAsciiDelete;
}

/**
 * Convert a character to lowercase (ASCII only).
 */
inline auto toLowerAscii(char c) -> char
{
  if (c >= 'A' && c <= 'Z')
  {
    return static_cast<char>(c + ('a' - 'A'));
  }
  return c;
}

/**
 * Convert a string_view to lowercase (ASCII only) and store in output string.
 */
inline auto toLowerAscii(std::string_view input, std::string &output) -> void
{
  output.clear();
  output.reserve(input.size());
  for (const char ch : input)
  {
    output.push_back(toLowerAscii(ch));
  }
}

/**
 * Parse a port number from a string_view.
 * Returns nullopt if invalid or out of range.
 */
inline auto parsePort(std::string_view portStr) -> std::optional<std::uint16_t>
{
  if (portStr.empty())
  {
    return std::nullopt;
  }

  std::uint32_t port = 0;
  for (const char ch : portStr)
  {
    if (std::isdigit(static_cast<unsigned char>(ch)) == 0)
    {
      return std::nullopt;
    }

    port = (port * kDecimalBase) + static_cast<std::uint32_t>(ch - '0');

    if (port > kMaxPort)
    {
      return std::nullopt;
    }
  }

  return static_cast<std::uint16_t>(port);
}

/**
 * Get the default port for a well-known scheme.
 * Returns 0 if scheme is unknown.
 */
inline auto getDefaultPort(std::string_view scheme) -> std::uint16_t
{
  if (scheme == "http")
  {
    return kPortHttp;
  }
  if (scheme == "https")
  {
    return kPortHttps;
  }
  if (scheme == "ftp")
  {
    return kPortFtp;
  }
  if (scheme == "ftps")
  {
    return kPortFtps;
  }
  if (scheme == "ws")
  {
    return kPortHttp;
  }
  if (scheme == "wss")
  {
    return kPortHttps;
  }
  return 0;
}

}  // namespace detail

/**
 * Parse an absolute URL string into its components.
 *
 * Requirements:
 * - Must have scheme:// format (absolute URLs only)
 * - Scheme and host are lowercased using ASCII rules
 * - IPv6 literals must be in [] brackets
 * - Userinfo (@) is rejected for security
 * - Whitespace and control characters are rejected
 * - Empty scheme or host is rejected
 *
 * @param rawUrl The URL string to parse
 * @return ParsedAbsoluteUrl if parsing succeeds, nullopt otherwise
 */
inline auto parseAbsoluteUrl(std::string_view rawUrl) -> std::optional<ParsedAbsoluteUrl>  // NOLINT(readability-function-cognitive-complexity)
{
  // Reject whitespace and control characters anywhere in the URL
  for (const char ch : rawUrl)
  {
    if (detail::isWhitespaceOrControl(ch))
    {
      return std::nullopt;
    }
  }

  // Must have scheme:// format
  const std::size_t schemeEnd = rawUrl.find("://");
  if (schemeEnd == std::string_view::npos || schemeEnd == 0)
  {
    return std::nullopt;
  }

  const std::string_view schemeView = rawUrl.substr(0, schemeEnd);
  if (schemeView.empty())
  {
    return std::nullopt;
  }

  // Validate scheme characters (must start with letter, then alphanumeric or +-.)
  for (std::size_t i = 0; i < schemeView.size(); ++i)
  {
    const char ch = schemeView.at(i);
    if (i == 0)
    {
      if (std::isalpha(static_cast<unsigned char>(ch)) == 0)
      {
        return std::nullopt;
      }
    }
    else
    {
      if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '+' && ch != '-' && ch != '.')
      {
        return std::nullopt;
      }
    }
  }

  // Extract authority and path+query (everything after ://)
  const std::size_t authorityStart = schemeEnd + 3;
  if (authorityStart >= rawUrl.size())
  {
    return std::nullopt;
  }

  const std::string_view remainder = rawUrl.substr(authorityStart);

  // Find end of authority (start of path, query, or fragment)
  std::size_t pathStart = remainder.find_first_of("/?#");
  if (pathStart == std::string_view::npos)
  {
    pathStart = remainder.size();
  }

  const std::string_view authority = remainder.substr(0, pathStart);
  if (authority.empty())
  {
    return std::nullopt;
  }

  // Reject userinfo (@ in authority before host/port)
  if (authority.find('@') != std::string_view::npos)
  {
    return std::nullopt;
  }

  // Parse host and port from authority
  std::string_view hostPort = authority;
  std::uint16_t explicitPort = 0;
  bool hasExplicitPortFlag = false;
  bool isIpv6 = false;

  // Check for IPv6 literal [addr] or [addr]:port
  if (!hostPort.empty() && hostPort.at(0) == '[')
  {
    const std::size_t bracketEnd = hostPort.find(']', 1);
    if (bracketEnd == std::string_view::npos)
    {
      return std::nullopt;  // Unclosed IPv6 bracket
    }

    // Extract host without brackets
    const std::string_view ipv6Content = hostPort.substr(1, bracketEnd - 1);
    if (ipv6Content.empty())
    {
      return std::nullopt;  // Empty IPv6 address
    }

    // Check for port after the closing bracket
    if (bracketEnd + 1 < hostPort.size())
    {
      if (hostPort.at(bracketEnd + 1) != ':')
      {
        return std::nullopt;  // Invalid character after IPv6 bracket
      }

      const std::string_view portStr = hostPort.substr(bracketEnd + 2);
      const std::optional<std::uint16_t> parsedPort = detail::parsePort(portStr);
      if (!parsedPort.has_value())
      {
        return std::nullopt;  // Invalid port
      }
      explicitPort = *parsedPort;
      hasExplicitPortFlag = true;
    }

    hostPort = ipv6Content;
    isIpv6 = true;
  }
  else
  {
    // Regular hostname or IPv4 - look for :port
    const std::size_t colonPos = hostPort.find(':');
    if (colonPos != std::string_view::npos)
    {
      const std::string_view portStr = hostPort.substr(colonPos + 1);
      const std::optional<std::uint16_t> parsedPort = detail::parsePort(portStr);
      if (!parsedPort.has_value())
      {
        return std::nullopt;  // Invalid port
      }
      explicitPort = *parsedPort;
      hasExplicitPortFlag = true;
      hostPort = hostPort.substr(0, colonPos);
    }
  }

  if (hostPort.empty())
  {
    return std::nullopt;
  }

  // Parse path and query from the remainder after authority
  std::string_view pathQuery;
  std::optional<std::string> queryStr = std::nullopt;
  bool hasQueryFlag = false;

  if (pathStart < remainder.size())
  {
    const char firstChar = remainder.at(pathStart);

    if (firstChar == '?')
    {
      // Query-only case: authority?query... - path is implied "/"
      pathQuery = std::string_view("/");

      // Extract query from after '?' until fragment or end
      std::size_t queryEnd = remainder.find('#', pathStart);
      if (queryEnd == std::string_view::npos)
      {
        queryEnd = remainder.size();
      }

      if (queryEnd > pathStart + 1)
      {
        queryStr = std::string(remainder.substr(pathStart + 1, queryEnd - pathStart - 1));
      }
      else
      {
        queryStr = std::string("");
      }
      hasQueryFlag = true;
    }
    else if (firstChar == '#')
    {
      // Fragment-only case: authority#fragment - path is implied "/"
      pathQuery = std::string_view("/");
    }
    else
    {
      // Regular path case (starts with '/')
      pathQuery = remainder.substr(pathStart);

      // Extract query if present
      const std::size_t queryStart = pathQuery.find('?');
      if (queryStart != std::string_view::npos)
      {
        // Find end of query (start of fragment)
        std::size_t fragmentStart = pathQuery.find('#', queryStart);
        if (fragmentStart == std::string_view::npos)
        {
          fragmentStart = pathQuery.size();
        }

        queryStr = std::string(pathQuery.substr(queryStart + 1, fragmentStart - queryStart - 1));
        hasQueryFlag = true;

        // Path is everything up to the query
        pathQuery = pathQuery.substr(0, queryStart);
      }

      // Handle fragment (ignore it for path, but don't include in path)
      const std::size_t fragmentPos = pathQuery.find('#');
      if (fragmentPos != std::string_view::npos)
      {
        pathQuery = pathQuery.substr(0, fragmentPos);
      }
    }
  }
  else
  {
    // No path/query/fragment - default to "/"
    pathQuery = std::string_view("/");
  }

  // Normalize empty path
  if (pathQuery.empty())
  {
    pathQuery = std::string_view("/");
  }

  // Build result
  ParsedAbsoluteUrl result;
  detail::toLowerAscii(schemeView, result.scheme);
  detail::toLowerAscii(hostPort, result.host);
  result.port = hasExplicitPortFlag ? explicitPort : detail::getDefaultPort(result.scheme);
  result.path = std::string(pathQuery);
  result.query = queryStr;
  result.ipv6Literal = isIpv6;
  result.hasExplicitPort = hasExplicitPortFlag;
  result.hasQuery = hasQueryFlag;

  return result;
}

}  // namespace mcp::detail
