#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <mcp/client/url_elicitation.hpp>

namespace mcp
{

inline auto formatUrlForConsent(std::string_view url) -> std::optional<UrlElicitationDisplayInfo>
{
  const std::size_t firstNonWhitespace = url.find_first_not_of(" \t\r\n");
  if (firstNonWhitespace == std::string_view::npos)
  {
    return std::nullopt;
  }

  const std::size_t lastNonWhitespace = url.find_last_not_of(" \t\r\n");
  const std::string_view trimmed = url.substr(firstNonWhitespace, (lastNonWhitespace - firstNonWhitespace) + 1);
  const std::size_t schemeSeparator = trimmed.find("://");
  if (schemeSeparator == std::string_view::npos || schemeSeparator == 0)
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

  std::string_view host = authority;
  if (host.front() == '[')
  {
    const std::size_t closingBracket = host.find(']');
    if (closingBracket == std::string_view::npos || closingBracket <= 1)
    {
      return std::nullopt;
    }

    host = host.substr(1, closingBracket - 1);
  }
  else
  {
    const std::size_t portSeparator = host.rfind(':');
    if (portSeparator != std::string_view::npos)
    {
      host = host.substr(0, portSeparator);
    }
  }

  if (host.empty())
  {
    return std::nullopt;
  }

  UrlElicitationDisplayInfo display;
  display.fullUrl = std::string(trimmed);
  display.domain = std::string(host);
  return display;
}

}  // namespace mcp
