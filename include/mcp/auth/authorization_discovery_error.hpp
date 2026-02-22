#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace mcp::auth
{

enum class AuthorizationDiscoveryErrorCode : std::uint8_t
{
  kInvalidInput,
  kSecurityPolicyViolation,
  kNetworkFailure,
  kMetadataValidation,
  kNotFound,
};

class AuthorizationDiscoveryError : public std::runtime_error
{
public:
  AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode code, std::string message);

  [[nodiscard]] auto code() const noexcept -> AuthorizationDiscoveryErrorCode;

private:
  AuthorizationDiscoveryErrorCode code_;
};

}  // namespace mcp::auth
