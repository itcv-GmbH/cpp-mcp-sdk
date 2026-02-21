#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <variant>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/transport/streamable_http_client_transport.hpp>

namespace mcp::transport
{

namespace
{

class StreamableHttpClientTransport final : public Transport
{
public:
  StreamableHttpClientTransport(http::StreamableHttpClientOptions options,
                                http::StreamableHttpClient::RequestExecutor requestExecutor,
                                std::function<void(const jsonrpc::Message &)> inboundMessageHandler)
    : client_(std::move(options), std::move(requestExecutor))
    , inboundMessageHandler_(std::move(inboundMessageHandler))
  {
  }

  auto attach(std::weak_ptr<Session> session) -> void override { static_cast<void>(session); }

  auto start() -> void override
  {
    const std::scoped_lock lock(mutex_);
    running_ = true;
  }

  auto stop() -> void override
  {
    const std::scoped_lock lock(mutex_);
    running_ = false;
  }

  auto isRunning() const noexcept -> bool override
  {
    const std::scoped_lock lock(mutex_);
    return running_;
  }

  auto send(jsonrpc::Message message) -> void override
  {
    std::function<void(const jsonrpc::Message &)> inboundMessageHandler;
    {
      const std::scoped_lock lock(mutex_);

      if (!running_)
      {
        throw std::runtime_error("HTTP transport must be running before send().");
      }

      inboundMessageHandler = inboundMessageHandler_;
    }

    const auto sendResult = client_.send(message);
    if (inboundMessageHandler == nullptr)
    {
      return;
    }

    for (const auto &inboundMessage : sendResult.messages)
    {
      inboundMessageHandler(inboundMessage);
    }

    if (sendResult.response.has_value())
    {
      std::visit([&inboundMessageHandler](const auto &typedResponse) -> void { inboundMessageHandler(jsonrpc::Message {typedResponse}); }, *sendResult.response);
    }
  }

private:
  mutable std::mutex mutex_;
  bool running_ = false;
  http::StreamableHttpClient client_;
  std::function<void(const jsonrpc::Message &)> inboundMessageHandler_;
};

}  // namespace

auto makeStreamableHttpClientTransport(http::StreamableHttpClientOptions options,
                                       http::StreamableHttpClient::RequestExecutor requestExecutor,
                                       std::function<void(const jsonrpc::Message &)> inboundMessageHandler) -> std::shared_ptr<Transport>
{
  return std::make_shared<StreamableHttpClientTransport>(std::move(options), std::move(requestExecutor), std::move(inboundMessageHandler));
}

}  // namespace mcp::transport
