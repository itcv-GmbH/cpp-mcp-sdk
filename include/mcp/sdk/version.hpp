#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <mcp/export.hpp>

namespace mcp
{
namespace sdk
{

inline constexpr std::string_view kSdkVersion = "0.1.0";
inline constexpr std::string_view kJsonRpcVersion = "2.0";
inline constexpr std::string_view kLatestProtocolVersion = "2025-11-25";
inline constexpr std::string_view kStableProtocolVersion = "2025-06-18";
inline constexpr std::string_view kFallbackProtocolVersion = "2025-03-26";
inline constexpr std::string_view kLegacyProtocolVersion = "2024-11-05";

class MCP_SDK_EXPORT NegotiatedProtocolVersion
{
public:
  auto setNegotiatedProtocolVersion(std::string protocolVersion) -> void { negotiatedProtocolVersion_ = std::move(protocolVersion); }

  auto clearNegotiatedProtocolVersion() noexcept -> void { negotiatedProtocolVersion_.reset(); }

  auto hasNegotiatedProtocolVersion() const noexcept -> bool { return negotiatedProtocolVersion_.has_value(); }

  auto negotiatedProtocolVersion() const noexcept -> std::optional<std::string_view>
  {
    if (!negotiatedProtocolVersion_)
    {
      return std::nullopt;
    }

    return *negotiatedProtocolVersion_;
  }

private:
  std::optional<std::string> negotiatedProtocolVersion_;
};

MCP_SDK_EXPORT auto getLibraryVersion() noexcept -> const char *;

inline auto getVersion() noexcept -> const char *
{
  return getLibraryVersion();
}

// Deprecated compatibility wrapper.
// NOLINTNEXTLINE(readability-identifier-naming)
inline auto get_version() noexcept -> const char *
{
  return getVersion();
}

}  // namespace sdk

// Deprecated: Backwards compatibility aliases
using sdk::getLibraryVersion;
using sdk::kFallbackProtocolVersion;
using sdk::kJsonRpcVersion;
using sdk::kLatestProtocolVersion;
using sdk::kLegacyProtocolVersion;
using sdk::kStableProtocolVersion;
using sdk::kSdkVersion;
using sdk::NegotiatedProtocolVersion;

}  // namespace mcp
