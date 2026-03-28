#pragma once

#include <stdexcept>
#include <string>

#include <mcp/export.hpp>

namespace mcp::auth
{

enum class LoopbackReceiverErrorCode : std::uint8_t
{
  kInvalidInput,
  kNetworkFailure,
  kStateMismatch,
  kTimeout,
  kProtocolViolation,
};

class MCP_SDK_EXPORT LoopbackReceiverError : public std::runtime_error
{
public:
  LoopbackReceiverError(LoopbackReceiverErrorCode code, const std::string &message);

  [[nodiscard]] auto code() const noexcept -> LoopbackReceiverErrorCode;

private:
  LoopbackReceiverErrorCode code_;
};

}  // namespace mcp::auth
