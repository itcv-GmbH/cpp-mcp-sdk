#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace mcp::auth
{

// NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
inline constexpr std::chrono::milliseconds kDefaultLoopbackAuthorizationTimeout = std::chrono::minutes(5);

struct LoopbackReceiverOptions
{
  std::string callbackPath = "/callback";
  std::chrono::milliseconds authorizationTimeout = kDefaultLoopbackAuthorizationTimeout;
  std::optional<std::string> successHtml;
};

}  // namespace mcp::auth
