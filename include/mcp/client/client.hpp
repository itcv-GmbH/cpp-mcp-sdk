#pragma once

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <mcp/lifecycle/session.hpp>

namespace mcp
{

class Client
{
public:
  static auto create(SessionOptions options = {}) -> std::shared_ptr<Client>;

  explicit Client(std::shared_ptr<Session> session);

  auto session() const noexcept -> const std::shared_ptr<Session> &;

  auto start() -> void;
  auto stop() -> void;

  auto sendRequest(std::string method, jsonrpc::JsonValue params, RequestOptions options = {}) -> std::future<jsonrpc::Response>;
  auto sendRequestAsync(std::string method, jsonrpc::JsonValue params, ResponseCallback callback, RequestOptions options = {}) -> void;
  auto sendNotification(std::string method, std::optional<jsonrpc::JsonValue> params = std::nullopt) -> void;

  auto registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void;
  auto registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void;

  auto negotiatedProtocolVersion() const noexcept -> std::optional<std::string_view>;

private:
  std::shared_ptr<Session> session_;
};

}  // namespace mcp
