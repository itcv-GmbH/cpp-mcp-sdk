#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace mcp
{

inline constexpr std::string_view kSdkVersion = "0.1.0";
inline constexpr std::string_view kJsonRpcVersion = "2.0";
inline constexpr std::string_view kLatestProtocolVersion = "2025-11-25";
inline constexpr std::string_view kFallbackProtocolVersion = "2025-03-26";
inline constexpr std::string_view kLegacyProtocolVersion = "2024-11-05";

class NegotiatedProtocolVersion
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

auto getLibraryVersion() noexcept -> const char *;

namespace sdk
{

inline auto get_version() noexcept -> const char *
{
  return getLibraryVersion();
}

}  // namespace sdk

}  // namespace mcp
