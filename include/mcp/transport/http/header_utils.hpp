#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mcp/detail/ascii.hpp>
#include <mcp/security/origin_policy.hpp>
#include <mcp/transport/http/header.hpp>
#include <mcp/version.hpp>

namespace mcp::transport::http
{

namespace detail
{

inline constexpr unsigned char kVisibleAsciiFirst = 0x21U;
inline constexpr unsigned char kVisibleAsciiLast = 0x7EU;
inline constexpr std::size_t kProtocolVersionLength = 10U;
inline constexpr std::size_t kProtocolVersionFirstDash = 4U;
inline constexpr std::size_t kProtocolVersionSecondDash = 7U;
inline constexpr std::uint16_t kHttpStatusOk = 200;
inline constexpr std::uint16_t kHttpStatusBadRequest = 400;
inline constexpr std::uint16_t kHttpStatusForbidden = 403;
inline constexpr std::uint16_t kHttpStatusNotFound = 404;
inline constexpr std::uint16_t kHttpStatusMethodNotAllowed = 405;
inline constexpr std::uint32_t kDefaultRetryMilliseconds = 1000U;

using ::mcp::detail::equalsIgnoreCaseAscii;
using ::mcp::detail::toLowerAscii;
using ::mcp::detail::trimAsciiWhitespace;

}  // namespace detail

inline constexpr std::string_view kHeaderAccept = "Accept";
inline constexpr std::string_view kHeaderContentType = "Content-Type";
inline constexpr std::string_view kHeaderOrigin = "Origin";
inline constexpr std::string_view kHeaderLastEventId = "Last-Event-ID";
inline constexpr std::string_view kHeaderMcpSessionId = "MCP-Session-Id";
inline constexpr std::string_view kHeaderMcpProtocolVersion = "MCP-Protocol-Version";
inline constexpr std::string_view kHeaderAuthorization = "Authorization";
inline constexpr std::string_view kHeaderWwwAuthenticate = "WWW-Authenticate";

inline auto setHeader(HeaderList &headers, std::string_view name, std::string value) -> void
{
  const auto existing = std::find_if(headers.begin(), headers.end(), [name](const Header &header) -> bool { return detail::equalsIgnoreCaseAscii(header.name, name); });

  if (existing != headers.end())
  {
    existing->value = std::move(value);
    return;
  }

  headers.push_back(Header {std::string(name), std::move(value)});
}

inline auto getHeader(const HeaderList &headers, std::string_view name) -> std::optional<std::string>
{
  const auto existing = std::find_if(headers.begin(), headers.end(), [name](const Header &header) -> bool { return detail::equalsIgnoreCaseAscii(header.name, name); });

  if (existing == headers.end())
  {
    return std::nullopt;
  }

  return existing->value;
}

inline auto isValidSessionId(std::string_view sessionId) noexcept -> bool
{
  if (sessionId.empty())
  {
    return false;
  }

  return std::all_of(sessionId.begin(),
                     sessionId.end(),
                     [](char character) -> bool
                     {
                       const auto byte = static_cast<unsigned char>(character);
                       return byte >= detail::kVisibleAsciiFirst && byte <= detail::kVisibleAsciiLast;
                     });
}

inline auto isValidProtocolVersion(std::string_view version) noexcept -> bool
{
  if (version.size() != detail::kProtocolVersionLength || version[detail::kProtocolVersionFirstDash] != '-' || version[detail::kProtocolVersionSecondDash] != '-')
  {
    return false;
  }

  for (std::size_t index = 0; index < version.size(); ++index)
  {
    if (index == detail::kProtocolVersionFirstDash || index == detail::kProtocolVersionSecondDash)
    {
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(version[index])) == 0)
    {
      return false;
    }
  }

  return true;
}

inline auto isSupportedProtocolVersion(std::string_view version, const std::vector<std::string> &supportedVersions) noexcept -> bool
{
  if (supportedVersions.empty())
  {
    return true;
  }

  return std::any_of(supportedVersions.begin(), supportedVersions.end(), [version](const std::string &supportedVersion) -> bool { return supportedVersion == version; });
}

}  // namespace mcp::transport::http