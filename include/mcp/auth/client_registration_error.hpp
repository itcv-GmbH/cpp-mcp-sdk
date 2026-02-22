#pragma once

#include <stdexcept>
#include <string>

namespace mcp::auth
{

enum class ClientRegistrationErrorCode : std::uint8_t
{
  kInvalidInput,
  kNetworkFailure,
  kMetadataValidation,
  kHostInteractionRequired,
};

class ClientRegistrationError : public std::runtime_error
{
public:
  ClientRegistrationError(ClientRegistrationErrorCode code, const std::string &message);

  [[nodiscard]] auto code() const noexcept -> ClientRegistrationErrorCode;

private:
  ClientRegistrationErrorCode code_;
};

}  // namespace mcp::auth
