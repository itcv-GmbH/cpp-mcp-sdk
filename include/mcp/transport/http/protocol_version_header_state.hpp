#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <mcp/export.hpp>
#include <mcp/transport/http/header.hpp>
#include <mcp/transport/http/header_utils.hpp>

namespace mcp::transport::http
{

class MCP_SDK_EXPORT ProtocolVersionHeaderState
{
public:
  auto setNegotiatedProtocolVersion(std::string_view protocolVersion) -> bool
  {
    const std::string_view normalizedVersion = detail::trimAsciiWhitespace(protocolVersion);
    if (!isValidProtocolVersion(normalizedVersion))
    {
      clear();
      return false;
    }

    negotiatedProtocolVersion_ = std::string(normalizedVersion);
    return true;
  }

  auto clear() noexcept -> void { negotiatedProtocolVersion_.reset(); }

  [[nodiscard]] auto negotiatedProtocolVersion() const noexcept -> const std::optional<std::string> & { return negotiatedProtocolVersion_; }

  auto replayToRequestHeaders(HeaderList &headers, bool isInitializeRequest = false) const -> void
  {
    if (!negotiatedProtocolVersion_.has_value() || isInitializeRequest)
    {
      return;
    }

    setHeader(headers, kHeaderMcpProtocolVersion, *negotiatedProtocolVersion_);
  }

private:
  std::optional<std::string> negotiatedProtocolVersion_;
};

}  // namespace mcp::transport::http