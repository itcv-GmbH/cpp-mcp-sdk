#pragma once

#include <cstdint>
#include <string>

namespace mcp::auth
{

enum class OAuthTokenVerificationStatus : std::uint8_t
{
  kValid,
  kInvalidToken,
  kInsufficientScope,
};

}  // namespace mcp::auth
