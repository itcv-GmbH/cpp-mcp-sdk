#pragma once

#include <memory>

#include <mcp/transport/http/http_client_options.hpp>
#include <mcp/transport/http/server_request.hpp>
#include <mcp/transport/http/server_response.hpp>

namespace mcp::transport::http
{

using HttpRequestHandler = std::function<ServerResponse(const ServerRequest &)>;

class HttpClientRuntime
{
public:
  explicit HttpClientRuntime(HttpClientOptions options);
  ~HttpClientRuntime();

  HttpClientRuntime(const HttpClientRuntime &) = delete;
  auto operator=(const HttpClientRuntime &) -> HttpClientRuntime & = delete;
  HttpClientRuntime(HttpClientRuntime &&other) noexcept;
  auto operator=(HttpClientRuntime &&other) noexcept -> HttpClientRuntime &;

  [[nodiscard]] auto execute(const http::ServerRequest &request) const -> http::ServerResponse;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::transport::http