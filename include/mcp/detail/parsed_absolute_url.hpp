#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mcp::detail
{

/**
 * Parsed representation of an absolute URL.
 * Provides a structured view of scheme, host, port, path, and query components
 * along with metadata flags for parsing characteristics.
 */
struct ParsedAbsoluteUrl
{
  std::string scheme;
  std::string host;
  std::uint16_t port = 0;
  std::string path;
  std::optional<std::string> query;

  bool ipv6Literal = false;
  bool hasExplicitPort = false;
  bool hasQuery = false;
};

}  // namespace mcp::detail
