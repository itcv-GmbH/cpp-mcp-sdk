#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/client/client.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/sdk/version.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/streamable_http_runner.hpp>
#include <mcp/server/tools.hpp>
#include <mcp/transport/all.hpp>
#include <mcp/transport/transport.hpp>

namespace
{

constexpr auto kWaitTimeout = std::chrono::seconds {2};

namespace mcp_http = mcp::transport::http;

auto makeInitializeRequestJson() -> std::string
{
  return R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":")" + std::string(mcp::kLatestProtocolVersion)
    + R"(","capabilities":{},"clientInfo":{"name":"test-client","version":"1.0.0"}}})";
}

auto makeInitializedNotificationJson() -> std::string
{
  return R"({"jsonrpc":"2.0","method":"notifications/initialized"})";
}

auto makeBlockingRequestJson() -> std::string
{
  return R"({"jsonrpc":"2.0","id":2,"method":"test/blocking","params":{}})";
}

auto makeJsonPostRequest(std::string body) -> mcp_http::ServerRequest
{
  mcp_http::ServerRequest request;
  request.method = mcp_http::ServerRequestMethod::kPost;
  request.path = "/mcp";
  request.body = std::move(body);
  mcp_http::setHeader(request.headers, mcp_http::kHeaderContentType, "application/json");
  return request;
}

class CapturingTransport final : public mcp::transport::Transport
{
public:
  auto attach(std::weak_ptr<mcp::Session> session) -> void override
  {
    const std::scoped_lock lock(mutex_);
    session_ = std::move(session);
  }

  auto start() -> void override
  {
    const std::scoped_lock lock(mutex_);
    running_ = true;
  }

  auto stop() -> void override
  {
    const std::scoped_lock lock(mutex_);
    running_ = false;
    ++stopCallCount_;
  }

  [[nodiscard]] auto isRunning() const noexcept -> bool override
  {
    const std::scoped_lock lock(mutex_);
    return running_;
  }

  auto send(mcp::jsonrpc::Message message) -> void override
  {
    const std::scoped_lock lock(mutex_);
    if (!running_)
    {
      throw std::runtime_error("CapturingTransport must be running before send().");
    }

    messages_.push_back(std::move(message));
    messagesCv_.notify_all();
  }

  [[nodiscard]] auto waitForMessageCount(std::size_t count, std::chrono::milliseconds timeout) const -> bool
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return messagesCv_.wait_for(lock, timeout, [&]() -> bool { return messages_.size() >= count; });
  }

  [[nodiscard]] auto stopCallCount() const -> std::size_t
  {
    const std::scoped_lock lock(mutex_);
    return stopCallCount_;
  }

private:
  mutable std::mutex mutex_;
  mutable std::condition_variable messagesCv_;
  bool running_ = false;
  std::weak_ptr<mcp::Session> session_;
  std::vector<mcp::jsonrpc::Message> messages_;
  std::size_t stopCallCount_ = 0;
};

struct CallbackState
{
  std::atomic<bool> invoked {false};
  std::promise<void> invokedPromise;
};

auto makeMinimalServer() -> std::shared_ptr<mcp::Server>
{
  auto server = mcp::Server::create();

  mcp::ToolDefinition definition;
  definition.name = "ping";
  definition.description = "ping tool";
  definition.inputSchema = mcp::jsonrpc::JsonValue::object();
  definition.inputSchema["type"] = "object";
  definition.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();

  server->registerTool(std::move(definition),
                       [](const mcp::ToolCallContext &) -> mcp::CallToolResult
                       {
                         mcp::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         result.content.push_back(mcp::jsonrpc::JsonValue::object());
                         result.content[0]["type"] = "text";
                         result.content[0]["text"] = "pong";
                         return result;
                       });

  return server;
}

auto waitUntilPortRebindable(std::uint16_t port, std::chrono::milliseconds timeout) -> bool
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline)
  {
    try
    {
      mcp::transport::http::HttpServerOptions options;
      options.endpoint.bindAddress = "127.0.0.1";
      options.endpoint.bindLocalhostOnly = true;
      options.endpoint.port = port;

      mcp::transport::http::HttpServerRuntime runtime(options);
      runtime.start();
      runtime.stop();
      return true;
    }
    catch (...)
    {
    }

    std::this_thread::sleep_for(std::chrono::milliseconds {20});
  }

  return false;
}

}  // namespace

TEST_CASE("Client destructor is bounded with in-flight async work", "[concurrency][destructor][client]")
{
  auto transport = std::make_shared<CapturingTransport>();
  auto client = mcp::Client::create();
  client->attachTransport(transport);
  client->start();

  auto callbackState = std::make_shared<CallbackState>();
  std::future<void> callbackFuture = callbackState->invokedPromise.get_future();

  client->sendRequestAsync("initialize",
                           mcp::jsonrpc::JsonValue::object(),
                           [callbackState](const mcp::jsonrpc::Response &) -> void
                           {
                             if (!callbackState->invoked.exchange(true))
                             {
                               callbackState->invokedPromise.set_value();
                             }
                           });

  REQUIRE(transport->waitForMessageCount(1, std::chrono::duration_cast<std::chrono::milliseconds>(kWaitTimeout)));

  std::future<void> destroyFuture = std::async(std::launch::async, [ownedClient = std::move(client)]() mutable -> void { ownedClient.reset(); });

  REQUIRE(destroyFuture.wait_for(kWaitTimeout) == std::future_status::ready);
  REQUIRE_NOTHROW(destroyFuture.get());

  REQUIRE(callbackFuture.wait_for(std::chrono::milliseconds {200}) == std::future_status::timeout);
  REQUIRE_FALSE(callbackState->invoked.load());
  REQUIRE_FALSE(transport->isRunning());
  REQUIRE(transport->stopCallCount() >= 1);
}

TEST_CASE("Destroying active StreamableHttpServerRunner is bounded and releases HTTP resources", "[concurrency][destructor][server_runner]")
{
  std::promise<void> handlerEnteredPromise;
  std::future<void> handlerEnteredFuture = handlerEnteredPromise.get_future();
  std::once_flag handlerEnteredOnce;

  std::promise<void> releaseHandlerPromise;
  std::shared_future<void> releaseHandlerFuture = releaseHandlerPromise.get_future().share();

  auto makeBlockingServer = [&handlerEnteredPromise, &handlerEnteredOnce, releaseHandlerFuture]() -> std::shared_ptr<mcp::Server>
  {
    auto server = makeMinimalServer();
    server->registerRequestHandler("test/blocking",
                                   [&handlerEnteredPromise, &handlerEnteredOnce, releaseHandlerFuture](const mcp::jsonrpc::RequestContext &,
                                                                                                       const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                   {
                                     std::call_once(handlerEnteredOnce, [&handlerEnteredPromise]() -> void { handlerEnteredPromise.set_value(); });

                                     return std::async(std::launch::async,
                                                       [releaseHandlerFuture, request]() -> mcp::jsonrpc::Response
                                                       {
                                                         releaseHandlerFuture.wait();

                                                         mcp::jsonrpc::SuccessResponse success;
                                                         success.id = request.id;
                                                         success.result = mcp::jsonrpc::JsonValue::object();
                                                         success.result["completed"] = true;
                                                         return mcp::jsonrpc::Response {std::move(success)};
                                                       });
                                   });

    return server;
  };

  auto runner = std::make_unique<mcp::StreamableHttpServerRunner>(makeBlockingServer);
  runner->start();

  REQUIRE(runner->isRunning());
  const std::uint16_t boundPort = runner->localPort();
  REQUIRE(boundPort != 0);

  mcp::transport::http::HttpClientOptions clientOptions;
  clientOptions.endpointUrl = "http://127.0.0.1:" + std::to_string(boundPort) + "/mcp";
  mcp::transport::http::HttpClientRuntime client(clientOptions);

  const mcp_http::ServerResponse initializeResponse = client.execute(makeJsonPostRequest(makeInitializeRequestJson()));
  REQUIRE(initializeResponse.statusCode == 200);

  const mcp_http::ServerResponse initializedResponse = client.execute(makeJsonPostRequest(makeInitializedNotificationJson()));
  REQUIRE(initializedResponse.statusCode == 202);

  std::future<mcp_http::ServerResponse> blockingRequestFuture =
    std::async(std::launch::async, [&client]() -> mcp_http::ServerResponse { return client.execute(makeJsonPostRequest(makeBlockingRequestJson())); });

  REQUIRE(handlerEnteredFuture.wait_for(kWaitTimeout) == std::future_status::ready);

  std::future<void> destroyFuture = std::async(std::launch::async, [&runner]() -> void { runner.reset(); });

  REQUIRE(destroyFuture.wait_for(std::chrono::milliseconds {100}) == std::future_status::timeout);

  releaseHandlerPromise.set_value();

  REQUIRE(destroyFuture.wait_for(kWaitTimeout) == std::future_status::ready);
  REQUIRE_NOTHROW(destroyFuture.get());

  REQUIRE(blockingRequestFuture.wait_for(kWaitTimeout) == std::future_status::ready);
  const mcp_http::ServerResponse blockingResponse = blockingRequestFuture.get();
  REQUIRE(blockingResponse.statusCode == 200);

  REQUIRE(waitUntilPortRebindable(boundPort, std::chrono::seconds {2}));
}
