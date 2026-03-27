#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <mcp/export.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/transport/http/request_kind.hpp>
#include <mcp/transport/http/request_validation_options.hpp>
#include <mcp/transport/http/server_request.hpp>
#include <mcp/transport/http/server_response.hpp>
#include <mcp/transport/http/session_lookup_state.hpp>
#include <mcp/transport/http/session_resolution.hpp>
#include <mcp/transport/http/streamable_http_server_options.hpp>
#include <mcp/transport/http/streamable_request_result.hpp>

namespace mcp::transport::http
{

using StreamableRequestHandler = std::function<StreamableRequestResult(const jsonrpc::RequestContext &, const jsonrpc::Request &)>;
using StreamableNotificationHandler = std::function<bool(const jsonrpc::RequestContext &, const jsonrpc::Notification &)>;
using StreamableResponseHandler = std::function<bool(const jsonrpc::RequestContext &, const jsonrpc::Response &)>;

class MCP_SDK_EXPORT StreamableHttpServer
{
public:
  explicit StreamableHttpServer(StreamableHttpServerOptions options = {});
  ~StreamableHttpServer();

  StreamableHttpServer(const StreamableHttpServer &) = delete;
  auto operator=(const StreamableHttpServer &) -> StreamableHttpServer & = delete;
  StreamableHttpServer(StreamableHttpServer &&other) noexcept;
  auto operator=(StreamableHttpServer &&other) noexcept -> StreamableHttpServer &;

  auto setRequestHandler(StreamableRequestHandler handler) -> void;
  auto setNotificationHandler(StreamableNotificationHandler handler) -> void;
  auto setResponseHandler(StreamableResponseHandler handler) -> void;

  auto upsertSession(std::string sessionId,
                     SessionLookupState state = SessionLookupState::kActive,
                     std::optional<std::string> negotiatedProtocolVersion = std::string(kLatestProtocolVersion)) -> void;
  auto setSessionState(std::string_view sessionId, SessionLookupState state) -> bool;

  [[nodiscard]] auto handleRequest(const ServerRequest &request) -> ServerResponse;
  [[nodiscard]] auto enqueueServerMessage(const jsonrpc::Message &message, const std::optional<std::string> &sessionId = std::nullopt) -> bool;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::transport::http