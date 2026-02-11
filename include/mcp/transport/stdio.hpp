#pragma once

#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/jsonrpc/router.hpp>
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

struct StdioAttachOptions
{
  bool allowStderrLogs = true;
  bool emitParseErrors = false;
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

  static auto run(jsonrpc::Router &router, StdioServerOptions options = {}) -> void;
  static auto attach(jsonrpc::Router &router, std::istream &serverStdout, std::ostream &serverStdin, StdioAttachOptions options = {}) -> void;
  static auto routeIncomingLine(jsonrpc::Router &router, std::string_view line, std::ostream &output, std::ostream *stderrOutput, StdioAttachOptions options = {}) -> bool;

private:
  enum class Mode
  {
    kServer,
    kClient,
  };

  auto stderrStream() -> std::ostream *;

  StdioServerOptions serverOptions_;
  StdioClientOptions clientOptions_;
  Mode mode_ = Mode::kServer;
  std::weak_ptr<Session> session_;
  bool running_ = false;
};

}  // namespace transport
}  // namespace mcp
