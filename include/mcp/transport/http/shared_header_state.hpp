#pragma once

#include <mutex>
#include <optional>
#include <string>

#include <mcp/transport/http/header.hpp>
#include <mcp/transport/http/header_utils.hpp>
#include <mcp/transport/http/protocol_version_header_state.hpp>
#include <mcp/transport/http/session_header_state.hpp>

namespace mcp::transport::http
{

class SharedHeaderState final
{
public:
  auto captureFromInitializeResponse(std::optional<std::string_view> sessionHeader, std::string_view protocolVersion) -> bool
  {
    const std::scoped_lock lock(mutex_);

    if (!sessionState_.captureFromInitializeResponse(sessionHeader))
    {
      return false;
    }

    if (!protocolVersion.empty())
    {
      protocolVersionState_.setNegotiatedProtocolVersion(protocolVersion);
    }

    return true;
  }

  auto clear() noexcept -> void
  {
    const std::scoped_lock lock(mutex_);
    sessionState_.clear();
    protocolVersionState_.clear();
  }

  [[nodiscard]] auto sessionId() const -> std::optional<std::string>
  {
    const std::scoped_lock lock(mutex_);
    return sessionState_.sessionId();
  }

  [[nodiscard]] auto replayOnSubsequentRequests() const noexcept -> bool
  {
    const std::scoped_lock lock(mutex_);
    return sessionState_.replayOnSubsequentRequests();
  }

  [[nodiscard]] auto negotiatedProtocolVersion() const -> std::optional<std::string>
  {
    const std::scoped_lock lock(mutex_);
    return protocolVersionState_.negotiatedProtocolVersion();
  }

  auto replayToRequestHeaders(HeaderList &headers, bool isInitializeRequest = false) const -> void
  {
    const std::scoped_lock lock(mutex_);
    sessionState_.replayToRequestHeaders(headers);
    protocolVersionState_.replayToRequestHeaders(headers, isInitializeRequest);
  }

private:
  mutable std::mutex mutex_;
  SessionHeaderState sessionState_;
  ProtocolVersionHeaderState protocolVersionState_;
};

}  // namespace mcp::transport::http