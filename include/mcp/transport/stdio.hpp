#pragma once

#include <string>
#include <vector>

#include <mcp/transport/transport.hpp>

namespace mcp
{
namespace transport
{

struct StdioServerOptions
{
  bool allowStderrLogs = true;
};

struct StdioClientOptions
{
  std::string executablePath;
  std::vector<std::string> arguments;
  std::vector<std::string> environment;
};

class StdioTransport final : public Transport
{
public:
  explicit StdioTransport(StdioServerOptions options = {});
  explicit StdioTransport(StdioClientOptions options);

  auto attach(std::weak_ptr<Session> session) -> void override;
  auto start() -> void override;
  auto stop() -> void override;
  auto isRunning() const noexcept -> bool override;
  auto send(jsonrpc::Message message) -> void override;

private:
  bool running_ = false;
};

}  // namespace transport
}  // namespace mcp
