#pragma once

#include <cstdint>
#include <memory>

#include <mcp/server/combined_server_runner_options.hpp>
#include <mcp/server/server_factory.hpp>

namespace mcp::server
{

class StdioServerRunner;
class StreamableHttpServerRunner;

class CombinedServerRunner final
{
public:
  CombinedServerRunner(ServerFactory serverFactory, CombinedServerRunnerOptions options);

  ~CombinedServerRunner();

  CombinedServerRunner(const CombinedServerRunner &) = delete;
  auto operator=(const CombinedServerRunner &) -> CombinedServerRunner & = delete;
  CombinedServerRunner(CombinedServerRunner &&other) noexcept;
  auto operator=(CombinedServerRunner &&other) noexcept -> CombinedServerRunner &;

  auto start() -> void;
  auto stop() -> void;
  auto runStdio() -> void;
  auto startHttp() -> void;
  auto stopHttp() -> void;
  [[nodiscard]] auto isHttpRunning() const noexcept -> bool;
  [[nodiscard]] auto localPort() const noexcept -> std::uint16_t;
  [[nodiscard]] auto options() const -> const CombinedServerRunnerOptions &;
  [[nodiscard]] auto stdioRunner() -> StdioServerRunner *;
  [[nodiscard]] auto httpRunner() -> StreamableHttpServerRunner *;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::server
