#pragma once

#include <cstdint>
#include <memory>

#include <mcp/server/server_factory.hpp>
#include <mcp/server/streamable_http_server_runner_options.hpp>

namespace mcp::server
{

class StreamableHttpServerRunner final
{
public:
  explicit StreamableHttpServerRunner(ServerFactory serverFactory);
  StreamableHttpServerRunner(ServerFactory serverFactory, StreamableHttpServerRunnerOptions options);

  ~StreamableHttpServerRunner();

  StreamableHttpServerRunner(const StreamableHttpServerRunner &) = delete;
  auto operator=(const StreamableHttpServerRunner &) -> StreamableHttpServerRunner & = delete;
  StreamableHttpServerRunner(StreamableHttpServerRunner &&other) noexcept;
  auto operator=(StreamableHttpServerRunner &&other) noexcept -> StreamableHttpServerRunner &;

  auto start() -> void;
  auto stop() noexcept -> void;
  [[nodiscard]] auto isRunning() const noexcept -> bool;
  [[nodiscard]] auto localPort() const noexcept -> std::uint16_t;
  [[nodiscard]] auto options() const -> const StreamableHttpServerRunnerOptions &;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::server
