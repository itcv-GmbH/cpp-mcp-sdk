#pragma once

#include <functional>
#include <memory>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/transport/http/server_request.hpp>
#include <mcp/transport/http/server_response.hpp>
#include <mcp/transport/http/streamable_http_client_options.hpp>
#include <mcp/transport/http/streamable_http_listen_result.hpp>
#include <mcp/transport/http/streamable_http_send_result.hpp>

namespace mcp::transport::http
{

class StreamableHttpClient
{
public:
  using RequestExecutor = std::function<ServerResponse(const ServerRequest &)>;

  explicit StreamableHttpClient(StreamableHttpClientOptions options, RequestExecutor requestExecutor);
  ~StreamableHttpClient();

  StreamableHttpClient(const StreamableHttpClient &) = delete;
  auto operator=(const StreamableHttpClient &) -> StreamableHttpClient & = delete;
  StreamableHttpClient(StreamableHttpClient &&other) noexcept;
  auto operator=(StreamableHttpClient &&other) noexcept -> StreamableHttpClient &;

  auto send(const jsonrpc::Message &message) -> StreamableHttpSendResult;
  auto openListenStream() -> StreamableHttpListenResult;
  auto pollListenStream() -> StreamableHttpListenResult;
  [[nodiscard]] auto hasActiveListenStream() const noexcept -> bool;

  // Explicitly terminates the MCP session by sending HTTP DELETE.
  // Servers may return HTTP 405 if they don't support client-initiated termination.
  // Returns true if termination was successful (2xx response), false if server doesn't support it (405).
  auto terminateSession() -> bool;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::transport::http