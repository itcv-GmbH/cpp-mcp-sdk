#pragma once

#include <optional>
#include <string>

#include <mcp/transport/http/header.hpp>
#include <mcp/transport/http/header_utils.hpp>

namespace mcp::transport::http
{

class SessionHeaderState
{
public:
  auto captureFromInitializeResponse(std::optional<std::string_view> sessionHeader) -> bool
  {
    if (!sessionHeader.has_value())
    {
      clear();
      return true;
    }

    const std::string_view normalizedSessionId = detail::trimAsciiWhitespace(*sessionHeader);
    if (!isValidSessionId(normalizedSessionId))
    {
      clear();
      return false;
    }

    sessionId_ = std::string(normalizedSessionId);
    replayOnSubsequentRequests_ = true;
    return true;
  }

  auto clear() noexcept -> void
  {
    sessionId_.reset();
    replayOnSubsequentRequests_ = false;
  }

  [[nodiscard]] auto sessionId() const noexcept -> const std::optional<std::string> & { return sessionId_; }

  [[nodiscard]] auto replayOnSubsequentRequests() const noexcept -> bool { return replayOnSubsequentRequests_ && sessionId_.has_value(); }

  auto replayToRequestHeaders(HeaderList &headers) const -> void
  {
    if (!replayOnSubsequentRequests())
    {
      return;
    }

    if (sessionId_.has_value())
    {
      setHeader(headers, kHeaderMcpSessionId, *sessionId_);
    }
  }

private:
  std::optional<std::string> sessionId_;
  bool replayOnSubsequentRequests_ = false;
};

}  // namespace mcp::transport::http