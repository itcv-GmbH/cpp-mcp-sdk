#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <mcp/lifecycle/session.hpp>

namespace mcp
{

inline constexpr std::size_t kDefaultMaxPaginationPages = 1024U;

struct ClientInitializeConfiguration
{
  std::optional<std::string> protocolVersion;
  std::optional<ClientCapabilities> capabilities;
  std::optional<Implementation> clientInfo;
};

}  // namespace mcp
