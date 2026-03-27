#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <mcp/export.hpp>
#include <mcp/transport/http/http_server_options.hpp>
#include <mcp/transport/http/server_request.hpp>
#include <mcp/transport/http/server_response.hpp>

namespace mcp::transport::http
{

using HttpRequestHandler = std::function<ServerResponse(const ServerRequest &)>;

class MCP_SDK_EXPORT HttpServerRuntime
{
public:
  explicit HttpServerRuntime(HttpServerOptions options = {});
  ~HttpServerRuntime();

  HttpServerRuntime(const HttpServerRuntime &) = delete;
  auto operator=(const HttpServerRuntime &) -> HttpServerRuntime & = delete;
  HttpServerRuntime(HttpServerRuntime &&other) noexcept;
  auto operator=(HttpServerRuntime &&other) noexcept -> HttpServerRuntime &;

  auto setRequestHandler(HttpRequestHandler handler) -> void;
  auto start() -> void;
  auto stop() noexcept -> void;
  [[nodiscard]] auto isRunning() const noexcept -> bool;
  [[nodiscard]] auto localPort() const noexcept -> std::uint16_t;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::transport::http