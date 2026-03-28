#pragma once

#include <stdexcept>
#include <string>

#include <mcp/export.hpp>

namespace mcp::auth
{

enum class ClientRegistrationErrorCode : std::uint8_t
{
  kInvalidInput,
  kNetworkFailure,
  kMetadataValidation,
  kHostInteractionRequired,
};

class MCP_SDK_EXPORT ClientRegistrationError : public std::runtime_error
{
public:
  ClientRegistrationError(ClientRegistrationErrorCode code, const std::string &message);

  [[nodiscard]] auto code() const noexcept -> ClientRegistrationErrorCode;

private:
  ClientRegistrationErrorCode code_;
};

}  // namespace mcp::auth
