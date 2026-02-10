#pragma once

#include <memory>
#include <string>

#include <mcp/lifecycle/session.hpp>

namespace mcp
{

class Server
{
public:
  static auto create(SessionOptions options = {}) -> std::shared_ptr<Server>;

  explicit Server(std::shared_ptr<Session> session);

  auto session() const noexcept -> const std::shared_ptr<Session> &;

  auto start() -> void;
  auto stop() -> void;

  auto registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void;
  auto registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void;

private:
  std::shared_ptr<Session> session_;
};

}  // namespace mcp
