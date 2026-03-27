#pragma once

#include <atomic>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <mcp/export.hpp>
#include <mcp/server/server_factory.hpp>
#include <mcp/server/stdio_server_runner_options.hpp>

namespace mcp::server
{

class MCP_SDK_EXPORT StdioServerRunner final
{
public:
  explicit StdioServerRunner(ServerFactory serverFactory);
  StdioServerRunner(ServerFactory serverFactory, StdioServerRunnerOptions options);

  ~StdioServerRunner();

  StdioServerRunner(const StdioServerRunner &) = delete;
  auto operator=(const StdioServerRunner &) -> StdioServerRunner & = delete;
  StdioServerRunner(StdioServerRunner &&other) noexcept;
  auto operator=(StdioServerRunner &&other) noexcept -> StdioServerRunner &;

  auto run() -> void;
  auto run(std::istream &input, std::ostream &output, std::ostream &errorStream) -> void;
  [[nodiscard]] auto startAsync() -> std::thread;
  auto stop() noexcept -> void;
  [[nodiscard]] auto options() const -> const StdioServerRunnerOptions &;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::server
