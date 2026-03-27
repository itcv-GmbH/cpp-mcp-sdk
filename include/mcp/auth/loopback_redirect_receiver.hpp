#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <mcp/auth/loopback_authorization_code.hpp>
#include <mcp/auth/loopback_receiver_options.hpp>
#include <mcp/export.hpp>

namespace mcp::auth
{

class MCP_SDK_EXPORT LoopbackRedirectReceiver
{
public:
  explicit LoopbackRedirectReceiver(LoopbackReceiverOptions options = {});
  ~LoopbackRedirectReceiver();

  LoopbackRedirectReceiver(const LoopbackRedirectReceiver &) = delete;
  auto operator=(const LoopbackRedirectReceiver &) -> LoopbackRedirectReceiver & = delete;
  LoopbackRedirectReceiver(LoopbackRedirectReceiver &&other) noexcept;
  auto operator=(LoopbackRedirectReceiver &&other) noexcept -> LoopbackRedirectReceiver &;

  auto start() -> void;
  auto stop() noexcept -> void;

  [[nodiscard]] auto isRunning() const noexcept -> bool;
  [[nodiscard]] auto localPort() const noexcept -> std::uint16_t;
  [[nodiscard]] auto callbackPath() const noexcept -> const std::string &;
  [[nodiscard]] auto redirectUri() const -> std::string;

  auto waitForAuthorizationCode(std::string expectedState, std::optional<std::chrono::milliseconds> timeoutOverride = std::nullopt) -> LoopbackAuthorizationCode;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::auth
