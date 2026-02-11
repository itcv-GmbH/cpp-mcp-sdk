#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

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

class LoopbackReceiverError : public std::runtime_error
{
public:
  LoopbackReceiverError(LoopbackReceiverErrorCode code, const std::string &message);

  [[nodiscard]] auto code() const noexcept -> LoopbackReceiverErrorCode;

private:
  LoopbackReceiverErrorCode code_;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
inline constexpr std::chrono::milliseconds kDefaultLoopbackAuthorizationTimeout = std::chrono::minutes(5);

struct LoopbackAuthorizationCode
{
  std::string code;
  std::string state;
};

struct LoopbackReceiverOptions
{
  std::string callbackPath = "/callback";
  std::chrono::milliseconds authorizationTimeout = kDefaultLoopbackAuthorizationTimeout;
  std::optional<std::string> successHtml;
};

class LoopbackRedirectReceiver
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
