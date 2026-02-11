#pragma once

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <mcp/lifecycle/session.hpp>

namespace mcp
{

struct ServerConfiguration
{
  SessionOptions sessionOptions;
  ServerCapabilities capabilities;
  std::optional<Implementation> serverInfo;
  std::optional<std::string> instructions;
};

class Server
{
public:
  static auto create(SessionOptions options = {}) -> std::shared_ptr<Server>;
  static auto create(ServerConfiguration configuration) -> std::shared_ptr<Server>;

  explicit Server(std::shared_ptr<Session> session);
  Server(std::shared_ptr<Session> session, ServerConfiguration configuration);

  auto configuration() const noexcept -> const ServerConfiguration &;

  auto session() const noexcept -> const std::shared_ptr<Session> &;

  auto start() -> void;
  auto stop() -> void;

  auto registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void;
  auto registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void;

  auto setOutboundMessageSender(jsonrpc::OutboundMessageSender sender) -> void;

  auto handleRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>;
  auto handleNotification(const jsonrpc::RequestContext &context, const jsonrpc::Notification &notification) -> void;
  auto handleResponse(const jsonrpc::RequestContext &context, const jsonrpc::Response &response) -> bool;

  auto sendRequest(const jsonrpc::RequestContext &context, jsonrpc::Request request, jsonrpc::OutboundRequestOptions options = {}) -> std::future<jsonrpc::Response>;
  auto sendNotification(const jsonrpc::RequestContext &context, jsonrpc::Notification notification) -> void;

private:
  auto configureSessionInitialization() -> void;
  auto registerCoreHandlers() -> void;

  auto isMethodEnabledByCapability(std::string_view method) const -> bool;
  static auto isCoreRequestMethod(std::string_view method) -> bool;

  std::shared_ptr<Session> session_;
  ServerConfiguration configuration_;
  jsonrpc::Router router_;
};

}  // namespace mcp
