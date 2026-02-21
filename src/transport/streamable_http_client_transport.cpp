#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <variant>

#include <mcp/detail/inbound_loop.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/transport/streamable_http_client_transport.hpp>

namespace mcp::transport
{

namespace
{

static constexpr std::string_view kInitializedNotificationMethod = "notifications/initialized";

auto isInitializedNotification(const jsonrpc::Message &message) -> bool
{
  return std::holds_alternative<jsonrpc::Notification>(message) && std::get<jsonrpc::Notification>(message).method == kInitializedNotificationMethod;
}

class StreamableHttpClientTransport final : public Transport
{
public:
  StreamableHttpClientTransport(http::StreamableHttpClientOptions options,
                                http::StreamableHttpClient::RequestExecutor requestExecutor,
                                std::function<void(const jsonrpc::Message &)> inboundMessageHandler)
    : options_(std::move(options))
    , client_(std::move(options_), std::move(requestExecutor))
    , inboundMessageHandler_(std::move(inboundMessageHandler))
    , enableGetListen_(options_.enableGetListen)
  {
  }

  ~StreamableHttpClientTransport() override { stop(); }

  auto attach(std::weak_ptr<Session> session) -> void override { static_cast<void>(session); }

  auto start() -> void override
  {
    const std::scoped_lock lock(mutex_);
    running_ = true;
  }

  auto stop() -> void override
  {
    // Signal the listen loop to stop
    listenLoopRunning_.store(false);

    // Stop and join the InboundLoop if it's running
    if (inboundLoop_ != nullptr)
    {
      inboundLoop_->stop();
      inboundLoop_->join();
      inboundLoop_.reset();
    }

    // Close any active listen stream (client-initiated closure per MCP spec section 6.3)
    if (client_.hasActiveListenStream())
    {
      try
      {
        client_.terminateSession();
      }
      catch (...)
      {
        // Ignore errors during session termination - we're stopping anyway
      }
    }

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
    bool shouldStartListenLoop = false;
    {
      const std::scoped_lock lock(mutex_);

      if (!running_)
      {
        throw std::runtime_error("HTTP transport must be running before send().");
      }

      inboundMessageHandler = inboundMessageHandler_;

      // Detect if this is the notifications/initialized message and we should start the listen loop
      if (enableGetListen_ && !listenLoopStarted_ && isInitializedNotification(message))
      {
        listenLoopStarted_ = true;
        shouldStartListenLoop = true;
      }
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

    // Start the listen loop after sending notifications/initialized
    if (shouldStartListenLoop && enableGetListen_)
    {
      startListenLoop();
    }
  }

private:
  auto startListenLoop() -> void
  {
    // Create the InboundLoop with the listen loop body
    auto loopBody = [this]() -> void { runListenLoop(); };
    inboundLoop_ = std::make_unique<detail::InboundLoop>(std::move(loopBody));
    inboundLoop_->start();
  }

  auto runListenLoop() -> void
  {
    // Continue while transport is running and GET listen is enabled
    while (listenLoopRunning_.load())
    {
      try
      {
        if (!client_.hasActiveListenStream())
        {
          // Open the listen stream
          const auto openResult = client_.openListenStream();

          // Handle HTTP 405 Method Not Allowed - server doesn't support GET
          if (openResult.statusCode == http::detail::kHttpStatusMethodNotAllowed)
          {
            enableGetListen_.store(false);
            return;
          }

          // Handle HTTP 404 - session terminated by server, stop the loop
          if (openResult.statusCode == http::detail::kHttpStatusNotFound)
          {
            return;
          }

          // Dispatch any messages from the initial open
          dispatchMessages(openResult.messages);

          // If stream is not open after initial open, stop the loop
          if (!openResult.streamOpen)
          {
            return;
          }
        }

        // Poll the listen stream for new messages
        const auto pollResult = client_.pollListenStream();

        // Handle HTTP 404 - session terminated by server, stop the loop
        if (pollResult.statusCode == http::detail::kHttpStatusNotFound)
        {
          return;
        }

        // Dispatch any messages from polling
        dispatchMessages(pollResult.messages);

        // If stream is closed, stop the loop
        if (!pollResult.streamOpen)
        {
          return;
        }
      }
      catch (...)
      {
        // Error containment: listen loop failures should not crash the process
        // Keep POST send operational even if listen loop fails
        // Log or handle the exception as appropriate
        break;
      }
    }
  }

  auto dispatchMessages(const std::vector<jsonrpc::Message> &messages) -> void
  {
    std::function<void(const jsonrpc::Message &)> handler;
    {
      const std::scoped_lock lock(mutex_);
      handler = inboundMessageHandler_;
    }

    if (handler == nullptr)
    {
      return;
    }

    for (const auto &message : messages)
    {
      handler(message);
    }
  }

  http::StreamableHttpClientOptions options_;
  mutable std::mutex mutex_;
  bool running_ = false;
  http::StreamableHttpClient client_;
  std::function<void(const jsonrpc::Message &)> inboundMessageHandler_;

  // GET listen loop state
  std::atomic<bool> listenLoopRunning_ {false};
  std::atomic<bool> enableGetListen_ {false};
  bool listenLoopStarted_ = false;
  std::unique_ptr<detail::InboundLoop> inboundLoop_;
};

}  // namespace

auto makeStreamableHttpClientTransport(http::StreamableHttpClientOptions options,
                                       http::StreamableHttpClient::RequestExecutor requestExecutor,
                                       std::function<void(const jsonrpc::Message &)> inboundMessageHandler) -> std::shared_ptr<Transport>
{
  return std::make_shared<StreamableHttpClientTransport>(std::move(options), std::move(requestExecutor), std::move(inboundMessageHandler));
}

}  // namespace mcp::transport
