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
#include <mcp/sdk/version.hpp>
#include <mcp/security/origin_policy.hpp>
#include <mcp/transport/http/header.hpp>

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
  for (Header &header : headers)
  {
    if (detail::equalsIgnoreCaseAscii(header.name, name))
    {
      header.value = std::move(value);
      return;
    }
  }

  headers.push_back(Header {std::string(name), std::move(value)});
}

inline auto getHeader(const HeaderList &headers, std::string_view name) -> std::optional<std::string>
{
  for (const Header &header : headers)
  {
    if (detail::equalsIgnoreCaseAscii(header.name, name))
    {
      return header.value;
    }
  }

  return std::nullopt;
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
  if (version.size() != detail::kProtocolVersionLength)
  {
    return false;
  }

  // NOLINTBEGIN(bugprone-exception-escape)
  // Size is checked above, so .at() will not throw
  if (version.at(detail::kProtocolVersionFirstDash) != '-' || version.at(detail::kProtocolVersionSecondDash) != '-')
  {
    return false;
  }

  for (std::size_t index = 0; index < version.size(); ++index)
  {
    if (index == detail::kProtocolVersionFirstDash || index == detail::kProtocolVersionSecondDash)
    {
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(version.at(index))) == 0)
    {
      return false;
    }
  }
  // NOLINTEND(bugprone-exception-escape)

  return true;
}

inline auto isSupportedProtocolVersion(std::string_view version, const std::vector<std::string> &supportedVersions) noexcept -> bool
{
  return std::any_of(supportedVersions.begin(), supportedVersions.end(), [version](const std::string &supportedVersion) -> bool { return supportedVersion == version; });
}

}  // namespace mcp::transport::http