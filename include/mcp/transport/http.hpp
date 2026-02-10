#pragma once

#include <optional>
#include <string>

#include <mcp/transport/transport.hpp>

namespace mcp
{
namespace transport
{

struct HttpServerOptions
{
  std::string endpointPath = "/mcp";
  bool validateOrigin = true;
  bool bindLocalhostOnly = true;
};

struct HttpClientOptions
{
  std::string endpointUrl;
  std::optional<std::string> sessionId;
  std::optional<std::string> bearerToken;
};

class HttpTransport final : public Transport
{
public:
  explicit HttpTransport(HttpServerOptions options);
  explicit HttpTransport(HttpClientOptions options);

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
