#pragma once

#include <cstdint>

namespace mcp::auth
{

enum class OAuthClientErrorCode : std::uint8_t
{
  kInvalidInput,
  kMetadataValidation,
  kSecurityPolicyViolation,
  kCryptoFailure,
  kNetworkFailure,
};

}  // namespace mcp::auth
