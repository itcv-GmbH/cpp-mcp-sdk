#pragma once

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/jsonrpc/router.hpp>
#include <mcp/security/limits.hpp>
#include <mcp/transport/transport.hpp>

namespace mcp::transport
{

inline constexpr std::int64_t kDefaultStdioShutdownTimeoutMilliseconds = 1500;

struct StdioServerOptions
{
  bool allowStderrLogs = true;
  security::RuntimeLimits limits;
};

struct StdioClientOptions
{
  std::string executablePath;
  std::vector<std::string> arguments;
  std::vector<std::string> environment;
  security::RuntimeLimits limits;
};

enum class StdioClientStderrMode : std::uint8_t
{
  kCapture,
  kForward,
  kIgnore,
};

struct StdioSubprocessSpawnOptions
{
  std::vector<std::string> argv;
  std::vector<std::string> envOverrides;
  std::string cwd;
  StdioClientStderrMode stderrMode = StdioClientStderrMode::kCapture;
};

struct StdioSubprocessShutdownOptions
{
  std::chrono::milliseconds waitForExitTimeout {kDefaultStdioShutdownTimeoutMilliseconds};
  std::chrono::milliseconds waitAfterTerminateTimeout {kDefaultStdioShutdownTimeoutMilliseconds};
};

class StdioSubprocess final
{
public:
  StdioSubprocess();
  ~StdioSubprocess();

  StdioSubprocess(const StdioSubprocess &) = delete;
  auto operator=(const StdioSubprocess &) -> StdioSubprocess & = delete;
  StdioSubprocess(StdioSubprocess &&other) noexcept;
  auto operator=(StdioSubprocess &&other) noexcept -> StdioSubprocess &;

  [[nodiscard]] auto valid() const noexcept -> bool;
  auto writeLine(std::string_view line) -> void;
  auto readLine(std::string &line) -> bool;
  auto closeStdin() noexcept -> void;
  [[nodiscard]] auto waitForExit(std::chrono::milliseconds timeout) -> bool;
  [[nodiscard]] auto shutdown(StdioSubprocessShutdownOptions options = {}) noexcept -> bool;
  [[nodiscard]] auto isRunning() -> bool;
  [[nodiscard]] auto exitCode() const -> std::optional<int>;
  [[nodiscard]] auto capturedStderr() const -> std::string;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  explicit StdioSubprocess(std::unique_ptr<Impl> impl);
  friend class StdioTransport;
};

struct StdioAttachOptions
{
  bool allowStderrLogs = true;
  bool emitParseErrors = false;
  security::RuntimeLimits limits;
};

class StdioTransport final : public Transport
{
public:
  [[deprecated(
    "StdioTransport instance constructors are deprecated. Use static StdioTransport::run() for servers or StdioTransport::spawnSubprocess() for clients. Instance-level Transport "
    "API is not supported for stdio.")]] explicit StdioTransport(StdioServerOptions options = {});
  [[deprecated(
    "StdioTransport instance constructors are deprecated. Use static StdioTransport::run() for servers or StdioTransport::spawnSubprocess() for clients. Instance-level Transport "
    "API is not supported for stdio.")]] explicit StdioTransport(const StdioClientOptions &options);

  [[deprecated("StdioTransport::attach() is deprecated and has no effect. Use static StdioTransport::run() or StdioTransport::attach(Router&, istream&, ostream&) instead.")]] auto
  attach(std::weak_ptr<Session> session) -> void override;
  [[deprecated("StdioTransport::start() is deprecated and has no effect. Use static StdioTransport::run() instead.")]] auto start() -> void override;
  [[deprecated("StdioTransport::stop() is deprecated and has no effect. Use static StdioTransport::run() instead.")]] auto stop() -> void override;
  [[deprecated("StdioTransport::isRunning() is deprecated. The instance-level Transport API is not supported for stdio.")]] auto isRunning() const noexcept -> bool override;
  [[deprecated(
    "StdioTransport::send() is deprecated. Use static StdioTransport::run() or StdioTransport::attach() instead. The instance-level Transport API is not supported for "
    "stdio.")]] auto
  send(jsonrpc::Message message) -> void override;

  static auto run(jsonrpc::Router &router, StdioServerOptions options = {}) -> void;
  static auto attach(jsonrpc::Router &router, std::istream &serverStdout, std::ostream &serverStdin, StdioAttachOptions options = {}) -> void;
  static auto routeIncomingLine(jsonrpc::Router &router, std::string_view line, std::ostream &output, std::ostream *stderrOutput, StdioAttachOptions options = {}) -> bool;
  [[nodiscard]] static auto spawnSubprocess(const StdioSubprocessSpawnOptions &options) -> StdioSubprocess;
};

}  // namespace mcp::transport
