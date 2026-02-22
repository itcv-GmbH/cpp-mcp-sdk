#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mcp::auth
{

enum class ClientAuthenticationMethod : std::uint8_t
{
  kNone,
  kClientSecretBasic,
  kClientSecretPost,
  kPrivateKeyJwt,
};

}  // namespace mcp::auth
