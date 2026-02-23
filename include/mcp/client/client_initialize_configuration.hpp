#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <mcp/lifecycle/session.hpp>

namespace mcp::client
{

inline constexpr std::size_t kDefaultMaxPaginationPages = 1024U;

struct ClientInitializeConfiguration
{
  std::optional<std::string> protocolVersion;
  std::optional<lifecycle::session::ClientCapabilities> capabilities;
  std::optional<lifecycle::session::Implementation> clientInfo;
};

}  // namespace mcp::client
