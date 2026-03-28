#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#include <mcp/export.hpp>

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

class MCP_SDK_EXPORT AuthorizationDiscoveryError : public std::runtime_error
{
public:
  AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode code, std::string message);

  [[nodiscard]] auto code() const noexcept -> AuthorizationDiscoveryErrorCode;

private:
  AuthorizationDiscoveryErrorCode code_;
};

}  // namespace mcp::auth
