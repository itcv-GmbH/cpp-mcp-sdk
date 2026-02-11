#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mcp/jsonrpc/router.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/transport/stdio.hpp>
#include <mcp/transport/transport.hpp>

namespace mcp
{

struct ClientInitializeConfiguration
{
  std::optional<std::string> protocolVersion;
  std::optional<ClientCapabilities> capabilities;
  std::optional<Implementation> clientInfo;
};

class Client
{
public:
  static auto create(SessionOptions options = {}) -> std::shared_ptr<Client>;

  explicit Client(std::shared_ptr<Session> session);

  auto session() const noexcept -> const std::shared_ptr<Session> &;

  auto attachTransport(std::shared_ptr<transport::Transport> transport) -> void;
  auto connectStdio(transport::StdioClientOptions options) -> void;
  auto connectHttp(const transport::HttpClientOptions &options) -> void;
  auto connectHttp(transport::http::StreamableHttpClientOptions options, transport::http::StreamableHttpClient::RequestExecutor requestExecutor) -> void;

  auto setInitializeConfiguration(ClientInitializeConfiguration configuration) -> void;
  auto initializeConfiguration() const -> ClientInitializeConfiguration;
  auto initialize(RequestOptions options = {}) -> std::future<jsonrpc::Response>;

  auto start() -> void;
  auto stop() -> void;

  auto sendRequest(std::string method, jsonrpc::JsonValue params, RequestOptions options = {}) -> std::future<jsonrpc::Response>;
  auto sendRequestAsync(std::string method, jsonrpc::JsonValue params, const ResponseCallback &callback, RequestOptions options = {}) -> void;
  auto sendNotification(std::string method, std::optional<jsonrpc::JsonValue> params = std::nullopt) -> void;

  auto registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void;
  auto registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void;

  auto handleRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>;
  auto handleNotification(const jsonrpc::RequestContext &context, const jsonrpc::Notification &notification) -> void;
  auto handleResponse(const jsonrpc::RequestContext &context, const jsonrpc::Response &response) -> bool;
  auto handleMessage(const jsonrpc::RequestContext &context, const jsonrpc::Message &message) -> void;

  auto negotiatedProtocolVersion() const noexcept -> std::optional<std::string_view>;
  auto negotiatedParameters() const -> std::optional<NegotiatedParameters>;
  auto negotiatedClientCapabilities() const -> std::optional<ClientCapabilities>;
  auto negotiatedServerCapabilities() const -> std::optional<ServerCapabilities>;

  auto supportedProtocolVersions() const -> std::vector<std::string>;

private:
  auto nextRequestId() -> jsonrpc::RequestId;
  auto applyInitializeDefaults(jsonrpc::Request &request) const -> void;
  auto dispatchOutboundMessage(jsonrpc::Message message) -> void;
  auto isPendingInitializeResponse(const jsonrpc::Response &response) const -> bool;

  mutable std::mutex mutex_;
  std::shared_ptr<Session> session_;
  jsonrpc::Router router_;
  std::shared_ptr<transport::Transport> transport_;
  ClientInitializeConfiguration initializeConfiguration_;
  std::optional<jsonrpc::RequestId> pendingInitializeRequestId_;
  std::int64_t nextRequestId_ = 1;
};

}  // namespace mcp
