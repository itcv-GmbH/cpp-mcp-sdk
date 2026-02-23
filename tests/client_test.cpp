#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
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
#include <mcp/client/elicitation.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/sdk/errors.hpp>
#include <mcp/sdk/version.hpp>
#include <mcp/server/prompts.hpp>
#include <mcp/server/resources.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/tools.hpp>
#include <mcp/transport/transport.hpp>

class RecordingTransport final : public mcp::transport::Transport
{
public:
  auto attach(std::weak_ptr<mcp::Session> session) -> void override
  {
    const std::scoped_lock lock(mutex_);
    attachedSession_ = std::move(session);
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
  }

  auto isRunning() const noexcept -> bool override
  {
    const std::scoped_lock lock(mutex_);
    return running_;
  }

  auto send(mcp::jsonrpc::Message message) -> void override
  {
    const std::scoped_lock lock(mutex_);
    if (!running_)
    {
      throw std::runtime_error("RecordingTransport must be running before send().");
    }

    messages_.push_back(std::move(message));
    messagesCv_.notify_all();
  }

  [[nodiscard]] auto messages() const -> std::vector<mcp::jsonrpc::Message>
  {
    const std::scoped_lock lock(mutex_);
    return messages_;
  }

  [[nodiscard]] auto waitForMessageCount(std::size_t count, std::chrono::milliseconds timeout) const -> bool
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return messagesCv_.wait_for(lock, timeout, [&]() -> bool { return messages_.size() >= count; });
  }

private:
  mutable std::mutex mutex_;
  mutable std::condition_variable messagesCv_;
  bool running_ = false;
  std::weak_ptr<mcp::Session> attachedSession_;
  std::vector<mcp::jsonrpc::Message> messages_;
};

class FlakyTransport final : public mcp::transport::Transport
{
public:
  explicit FlakyTransport(std::size_t failuresBeforeSuccess)
    : failuresRemaining_(failuresBeforeSuccess)
  {
  }

  auto attach(std::weak_ptr<mcp::Session> session) -> void override
  {
    const std::scoped_lock lock(mutex_);
    attachedSession_ = std::move(session);
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
  }

  auto isRunning() const noexcept -> bool override
  {
    const std::scoped_lock lock(mutex_);
    return running_;
  }

  auto send(mcp::jsonrpc::Message message) -> void override
  {
    const std::scoped_lock lock(mutex_);
    if (!running_)
    {
      throw std::runtime_error("FlakyTransport must be running before send().");
    }

    if (failuresRemaining_ > 0)
    {
      --failuresRemaining_;
      throw std::runtime_error("Injected local send failure.");
    }

    messages_.push_back(std::move(message));
  }

  [[nodiscard]] auto messages() const -> std::vector<mcp::jsonrpc::Message>
  {
    const std::scoped_lock lock(mutex_);
    return messages_;
  }

private:
  mutable std::mutex mutex_;
  bool running_ = false;
  std::size_t failuresRemaining_ = 0;
  std::weak_ptr<mcp::Session> attachedSession_;
  std::vector<mcp::jsonrpc::Message> messages_;
};

class InMemoryClientServerTransport final : public mcp::transport::Transport
{
public:
  InMemoryClientServerTransport(std::shared_ptr<mcp::Server> server, std::weak_ptr<mcp::Client> client)
    : server_(std::move(server))
    , client_(std::move(client))
  {
    if (!server_)
    {
      throw std::invalid_argument("In-memory transport requires a server instance");
    }

    context_.sessionId = "in-memory-session";
  }

  auto attach(std::weak_ptr<mcp::Session> session) -> void override
  {
    const std::scoped_lock lock(mutex_);
    attachedSession_ = std::move(session);
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
  }

  auto isRunning() const noexcept -> bool override
  {
    const std::scoped_lock lock(mutex_);
    return running_;
  }

  auto send(mcp::jsonrpc::Message message) -> void override
  {
    std::shared_ptr<mcp::Server> server;
    std::weak_ptr<mcp::Client> client;

    {
      const std::scoped_lock lock(mutex_);
      if (!running_)
      {
        throw std::runtime_error("In-memory transport must be running before send().");
      }

      server = server_;
      client = client_;
    }

    if (std::holds_alternative<mcp::jsonrpc::Request>(message))
    {
      const auto response = server->handleRequest(context_, std::get<mcp::jsonrpc::Request>(message)).get();
      if (const auto clientRef = client.lock())
      {
        static_cast<void>(clientRef->handleResponse(context_, response));
      }
      return;
    }

    if (std::holds_alternative<mcp::jsonrpc::Notification>(message))
    {
      server->handleNotification(context_, std::get<mcp::jsonrpc::Notification>(message));
      return;
    }

    if (std::holds_alternative<mcp::jsonrpc::SuccessResponse>(message))
    {
      static_cast<void>(server->handleResponse(context_, mcp::jsonrpc::Response {std::get<mcp::jsonrpc::SuccessResponse>(message)}));
      return;
    }

    static_cast<void>(server->handleResponse(context_, mcp::jsonrpc::Response {std::get<mcp::jsonrpc::ErrorResponse>(message)}));
  }

private:
  mutable std::mutex mutex_;
  bool running_ = false;
  mcp::jsonrpc::RequestContext context_;
  std::weak_ptr<mcp::Session> attachedSession_;
  std::shared_ptr<mcp::Server> server_;
  std::weak_ptr<mcp::Client> client_;
};

static auto requestParamsOrObject(const mcp::jsonrpc::Request &request) -> mcp::jsonrpc::JsonValue
{
  if (!request.params.has_value())
  {
    return mcp::jsonrpc::JsonValue::object();
  }

  return *request.params;
}

static auto makeSuccessfulInitializeResponse(const mcp::jsonrpc::RequestId &requestId, mcp::jsonrpc::JsonValue capabilities = mcp::jsonrpc::JsonValue::object())
  -> mcp::jsonrpc::Response
{
  mcp::jsonrpc::SuccessResponse response;
  response.id = requestId;
  response.result = mcp::jsonrpc::JsonValue::object();
  response.result["protocolVersion"] = std::string(mcp::kLatestProtocolVersion);
  response.result["capabilities"] = std::move(capabilities);
  response.result["serverInfo"] = mcp::jsonrpc::JsonValue::object();
  response.result["serverInfo"]["name"] = "server-test";
  response.result["serverInfo"]["version"] = "1.2.3";
  return mcp::jsonrpc::Response {std::move(response)};
}

static constexpr std::size_t kRoundTripItemCount = 53;

static auto makeFailedInitializeResponse(const mcp::jsonrpc::RequestId &requestId) -> mcp::jsonrpc::Response
{
  mcp::jsonrpc::ErrorResponse response;
  response.id = requestId;
  response.error.code = static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams);
  response.error.message = "Initialize rejected";
  return mcp::jsonrpc::Response {std::move(response)};
}

static auto makeToolDefinition(std::string name) -> mcp::ToolDefinition
{
  mcp::ToolDefinition definition;
  definition.name = std::move(name);
  definition.description = "tool description";
  definition.inputSchema = mcp::jsonrpc::JsonValue::object();
  definition.inputSchema["type"] = "object";
  definition.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();
  definition.inputSchema["properties"]["value"] = mcp::jsonrpc::JsonValue::object();
  definition.inputSchema["properties"]["value"]["type"] = "string";
  definition.inputSchema["required"] = mcp::jsonrpc::JsonValue::array();
  definition.inputSchema["required"].push_back("value");
  return definition;
}

static auto makePromptDefinition(std::string name) -> mcp::PromptDefinition
{
  mcp::PromptDefinition definition;
  definition.name = std::move(name);
  definition.description = "prompt description";

  mcp::PromptArgumentDefinition argument;
  argument.name = "topic";
  argument.required = true;
  definition.arguments.push_back(std::move(argument));
  return definition;
}

TEST_CASE("Client blocks feature requests before initialize", "[client][core][lifecycle]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  REQUIRE_THROWS_AS(client->sendRequest("tools/list", mcp::jsonrpc::JsonValue::object()), mcp::LifecycleError);
}

TEST_CASE("Client initialize defaults to latest supported version and auto-sends initialized", "[client][core][initialize]")
{
  mcp::lifecycle::session::SessionOptions options;
  options.supportedProtocolVersions = {"2024-11-05", "2025-11-25"};

  auto client = mcp::Client::create(options);
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  auto initializeFuture = client->initialize();

  const auto outboundAfterInitialize = transport->messages();
  REQUIRE(outboundAfterInitialize.size() == 1);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(outboundAfterInitialize.front()));

  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundAfterInitialize.front());
  REQUIRE(initializeRequest.method == "initialize");
  const auto initializeParams = requestParamsOrObject(initializeRequest);
  REQUIRE(initializeParams["protocolVersion"].as<std::string>() == "2025-11-25");

  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));

  const mcp::jsonrpc::Response initializeResponse = initializeFuture.get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initializeResponse));

  const auto outboundAfterHandshake = transport->messages();
  REQUIRE(outboundAfterHandshake.size() == 2);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Notification>(outboundAfterHandshake.back()));
  REQUIRE(std::get<mcp::jsonrpc::Notification>(outboundAfterHandshake.back()).method == "notifications/initialized");

  const auto negotiatedVersion = client->negotiatedProtocolVersion();
  REQUIRE(negotiatedVersion.has_value());
  REQUIRE(negotiatedVersion.value_or("") == mcp::kLatestProtocolVersion);
}

TEST_CASE("Client initialize configuration applies declared capabilities", "[client][core][initialize]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::RootsCapability roots;
  roots.listChanged = true;

  mcp::SamplingCapability sampling;
  sampling.context = true;
  sampling.tools = true;

  mcp::ElicitationCapability elicitation;
  elicitation.form = true;
  elicitation.url = true;

  mcp::TasksCapability tasks;
  tasks.list = true;
  tasks.cancel = true;
  tasks.samplingCreateMessage = true;
  tasks.elicitationCreate = true;

  mcp::ClientInitializeConfiguration configuration;
  configuration.protocolVersion = "2024-11-05";
  configuration.capabilities = mcp::ClientCapabilities(roots, sampling, elicitation, tasks, std::nullopt);
  configuration.clientInfo = mcp::Implementation("host-app", "9.8.7");
  client->setInitializeConfiguration(configuration);

  static_cast<void>(client->initialize());

  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(outboundMessages.front()));

  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  const auto params = requestParamsOrObject(initializeRequest);
  REQUIRE(params["protocolVersion"].as<std::string>() == "2024-11-05");
  REQUIRE(params["capabilities"].contains("roots"));
  REQUIRE(params["capabilities"]["roots"]["listChanged"].as<bool>());
  REQUIRE(params["capabilities"]["sampling"].contains("context"));
  REQUIRE(params["capabilities"]["sampling"].contains("tools"));
  REQUIRE(params["capabilities"]["elicitation"].contains("form"));
  REQUIRE(params["capabilities"]["elicitation"].contains("url"));
  REQUIRE(params["capabilities"]["tasks"].contains("list"));
  REQUIRE(params["capabilities"]["tasks"].contains("cancel"));
  REQUIRE(params["capabilities"]["tasks"]["requests"]["sampling"].contains("createMessage"));
  REQUIRE(params["capabilities"]["tasks"]["requests"]["elicitation"].contains("create"));
  REQUIRE(params["clientInfo"]["name"].as<std::string>() == "host-app");
  REQUIRE(params["clientInfo"]["version"].as<std::string>() == "9.8.7");
}

TEST_CASE("Client only allows ping requests while initialize is in-flight", "[client][core][lifecycle]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  auto initializeFuture = client->initialize();
  REQUIRE_NOTHROW(client->sendRequest("ping", mcp::jsonrpc::JsonValue::object()));
  REQUIRE_THROWS_AS(client->sendRequest("tools/list", mcp::jsonrpc::JsonValue::object()), mcp::LifecycleError);

  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 2);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(outboundMessages[1]));
  REQUIRE(std::get<mcp::jsonrpc::Request>(outboundMessages[1]).method == "ping");

  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  static_cast<void>(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());
}

TEST_CASE("Client surfaces initialize failures without sending initialized notification", "[client][core][initialize]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  auto initializeFuture = client->initialize();

  const auto outboundBeforeError = transport->messages();
  REQUIRE(outboundBeforeError.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundBeforeError.front());

  REQUIRE_THROWS_AS(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeFailedInitializeResponse(initializeRequest.id)), mcp::LifecycleError);

  const mcp::jsonrpc::Response initializeResult = initializeFuture.get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(initializeResult));

  const auto outboundAfterError = transport->messages();
  REQUIRE(outboundAfterError.size() == 1);
  REQUIRE(client->session()->state() == mcp::SessionState::kCreated);
}

TEST_CASE("Client recovers from local initialize send failure and allows retry", "[client][core][initialize]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<FlakyTransport>(1);
  client->attachTransport(transport);
  client->start();

  auto firstInitializeFuture = client->initialize();
  const mcp::jsonrpc::Response firstInitializeResult = firstInitializeFuture.get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(firstInitializeResult));
  REQUIRE(client->session()->state() == mcp::SessionState::kCreated);

  auto retryInitializeFuture = client->initialize();
  REQUIRE(client->session()->state() == mcp::SessionState::kInitializing);

  const auto outboundAfterRetry = transport->messages();
  REQUIRE(outboundAfterRetry.size() == 1);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(outboundAfterRetry.front()));
  const auto &retryInitializeRequest = std::get<mcp::jsonrpc::Request>(outboundAfterRetry.front());
  REQUIRE(retryInitializeRequest.method == "initialize");

  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(retryInitializeRequest.id)));

  const mcp::jsonrpc::Response retryInitializeResult = retryInitializeFuture.get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(retryInitializeResult));
  REQUIRE(client->session()->state() == mcp::SessionState::kOperating);

  const auto finalOutboundMessages = transport->messages();
  REQUIRE(finalOutboundMessages.size() == 2);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Notification>(finalOutboundMessages.back()));
  REQUIRE(std::get<mcp::jsonrpc::Notification>(finalOutboundMessages.back()).method == "notifications/initialized");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Client sendRequestAsync is non-blocking and invokes callback asynchronously", "[client][core][async]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  std::promise<void> sendRequestAsyncReturnedPromise;
  auto sendRequestAsyncReturnedFuture = sendRequestAsyncReturnedPromise.get_future();

  std::mutex callbackMutex;
  std::thread::id callbackThreadId;
  std::promise<mcp::jsonrpc::Response> callbackResponsePromise;
  auto callbackResponseFuture = callbackResponsePromise.get_future();

  auto callerFuture = std::async(std::launch::async,
                                 [&]() -> std::thread::id
                                 {
                                   client->sendRequestAsync("initialize",
                                                            mcp::jsonrpc::JsonValue::object(),
                                                            [&](const mcp::jsonrpc::Response &response) -> void
                                                            {
                                                              {
                                                                const std::scoped_lock lock(callbackMutex);
                                                                callbackThreadId = std::this_thread::get_id();
                                                              }
                                                              callbackResponsePromise.set_value(response);
                                                            });
                                   sendRequestAsyncReturnedPromise.set_value();
                                   return std::this_thread::get_id();
                                 });

  REQUIRE(transport->waitForMessageCount(1, std::chrono::milliseconds {500}));
  REQUIRE(sendRequestAsyncReturnedFuture.wait_for(std::chrono::milliseconds {500}) == std::future_status::ready);
  REQUIRE(callbackResponseFuture.wait_for(std::chrono::milliseconds {0}) == std::future_status::timeout);

  const auto callerThreadId = callerFuture.get();

  const auto outboundMessages = transport->messages();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(outboundMessages.front()));
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());

  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));

  REQUIRE(callbackResponseFuture.wait_for(std::chrono::milliseconds {500}) == std::future_status::ready);
  const auto callbackResult = callbackResponseFuture.get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(callbackResult));

  {
    const std::scoped_lock lock(callbackMutex);
    REQUIRE(callbackThreadId != callerThreadId);
  }
}

TEST_CASE("Client stop prevents pending async callbacks and teardown does not hang", "[client][core][async][teardown]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  std::atomic<bool> callbackStarted = false;
  std::promise<void> callbackStartedPromise;
  auto callbackStartedFuture = callbackStartedPromise.get_future();

  client->sendRequestAsync("initialize",
                           mcp::jsonrpc::JsonValue::object(),
                           [&callbackStarted, &callbackStartedPromise](const mcp::jsonrpc::Response &response) -> void
                           {
                             static_cast<void>(response);
                             if (!callbackStarted.exchange(true))
                             {
                               callbackStartedPromise.set_value();
                             }
                           });

  REQUIRE(transport->waitForMessageCount(1, std::chrono::milliseconds {500}));
  const auto outboundMessages = transport->messages();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(outboundMessages.front()));
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());

  std::future<void> stopFuture = std::async(std::launch::async, [&client]() -> void { client->stop(); });
  REQUIRE(stopFuture.wait_for(std::chrono::milliseconds {500}) == std::future_status::ready);
  stopFuture.get();

  REQUIRE_THROWS_AS(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)), mcp::LifecycleError);
  REQUIRE(callbackStartedFuture.wait_for(std::chrono::milliseconds {150}) == std::future_status::timeout);
  REQUIRE_FALSE(callbackStarted.load());

  std::future<void> destroyFuture = std::async(std::launch::async, [client = std::move(client)]() mutable -> void { client.reset(); });
  REQUIRE(destroyFuture.wait_for(std::chrono::milliseconds {500}) == std::future_status::ready);
  destroyFuture.get();
}

TEST_CASE("Client destructor stops async callback execution without explicit stop", "[client][core][async][destructor]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  std::atomic<bool> callbackStarted = false;
  std::promise<void> callbackStartedPromise;
  auto callbackStartedFuture = callbackStartedPromise.get_future();

  client->sendRequestAsync("initialize",
                           mcp::jsonrpc::JsonValue::object(),
                           [&callbackStarted, &callbackStartedPromise](const mcp::jsonrpc::Response &response) -> void
                           {
                             static_cast<void>(response);
                             if (!callbackStarted.exchange(true))
                             {
                               callbackStartedPromise.set_value();
                             }
                           });

  REQUIRE(transport->waitForMessageCount(1, std::chrono::milliseconds {500}));

  std::future<void> destroyFuture = std::async(std::launch::async, [client = std::move(client)]() mutable -> void { client.reset(); });
  REQUIRE(destroyFuture.wait_for(std::chrono::milliseconds {500}) == std::future_status::ready);
  destroyFuture.get();

  REQUIRE(callbackStartedFuture.wait_for(std::chrono::milliseconds {150}) == std::future_status::timeout);
  REQUIRE_FALSE(callbackStarted.load());
}

TEST_CASE("Client callback-thread stop plus immediate destruction quiesces async sendRequestAsync workers", "[client][core][async][teardown]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  constexpr std::size_t requestCount = 16;
  auto clientSlot = std::make_shared<std::shared_ptr<mcp::Client>>(client);
  std::mutex clientSlotMutex;

  std::atomic<std::size_t> callbackCount = 0;
  std::atomic<bool> callbackStopCompleted = false;
  std::atomic<bool> callbackStopRaisedException = false;
  std::promise<void> firstCallbackPromise;
  auto firstCallbackFuture = firstCallbackPromise.get_future();

  for (std::size_t requestIndex = 0; requestIndex < requestCount; ++requestIndex)
  {
    const std::string method = requestIndex == 0 ? "initialize" : "ping";

    client->sendRequestAsync(
      method,
      mcp::jsonrpc::JsonValue::object(),
      [clientSlot, &clientSlotMutex, &callbackCount, &callbackStopCompleted, &callbackStopRaisedException, &firstCallbackPromise](const mcp::jsonrpc::Response &response) -> void
      {
        static_cast<void>(response);
        const std::size_t callbackIndex = callbackCount.fetch_add(1);
        if (callbackIndex != 0)
        {
          return;
        }

        std::shared_ptr<mcp::Client> callbackClient;
        {
          const std::scoped_lock lock(clientSlotMutex);
          callbackClient = std::move(*clientSlot);
        }

        if (callbackClient)
        {
          try
          {
            callbackClient->stop();
            callbackStopCompleted.store(true);
          }
          catch (const std::exception &)
          {
            callbackStopRaisedException.store(true);
          }

          callbackClient.reset();
        }

        firstCallbackPromise.set_value();
      });
  }

  REQUIRE(transport->waitForMessageCount(requestCount, std::chrono::milliseconds {1000}));
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == requestCount);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(outboundMessages.front()));
  const auto &firstRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(firstRequest.method == "initialize");

  mcp::Client *rawClient = client.get();
  REQUIRE(rawClient != nullptr);
  client.reset();

  REQUIRE(rawClient->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(firstRequest.id)));
  REQUIRE(firstCallbackFuture.wait_for(std::chrono::milliseconds {1000}) == std::future_status::ready);

  REQUIRE(callbackStopCompleted.load());
  REQUIRE_FALSE(callbackStopRaisedException.load());
  std::this_thread::sleep_for(std::chrono::milliseconds {150});
  REQUIRE(callbackCount.load() == 1);

  const auto transportStopDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds {1000};
  while (transport->isRunning() && std::chrono::steady_clock::now() < transportStopDeadline)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds {10});
  }

  REQUIRE_FALSE(transport->isRunning());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Client exposes negotiated capabilities after initialize", "[client][core][negotiation]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::RootsCapability roots;
  roots.listChanged = true;

  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(roots, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  client->setInitializeConfiguration(configuration);

  auto initializeFuture = client->initialize();

  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());

  mcp::jsonrpc::JsonValue serverCapabilities = mcp::jsonrpc::JsonValue::object();
  serverCapabilities["tools"] = mcp::jsonrpc::JsonValue::object();
  serverCapabilities["tools"]["listChanged"] = true;
  static_cast<void>(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id, std::move(serverCapabilities))));
  static_cast<void>(initializeFuture.get());

  const auto negotiatedClientCapabilities = client->negotiatedClientCapabilities();
  REQUIRE(negotiatedClientCapabilities.has_value());
  const auto negotiatedRoots = negotiatedClientCapabilities.has_value() ? negotiatedClientCapabilities->roots() : std::optional<mcp::RootsCapability> {};
  REQUIRE(negotiatedRoots.has_value());
  REQUIRE(negotiatedRoots.value_or(mcp::RootsCapability {}).listChanged);

  const auto negotiatedServerCapabilities = client->negotiatedServerCapabilities();
  REQUIRE(negotiatedServerCapabilities.has_value());
  const auto negotiatedTools = negotiatedServerCapabilities.has_value() ? negotiatedServerCapabilities->tools() : std::optional<mcp::ToolsCapability> {};
  REQUIRE(negotiatedTools.has_value());
  REQUIRE(negotiatedTools.value_or(mcp::ToolsCapability {}).listChanged);
}

TEST_CASE("Client convenience APIs enforce negotiated capability gating", "[client][features][capabilities]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));
  auto client = mcp::Client::create();
  auto transport = std::make_shared<InMemoryClientServerTransport>(server, client);
  client->attachTransport(transport);
  client->start();

  const auto initializeResponse = client->initialize().get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initializeResponse));

  REQUIRE_THROWS_AS(client->listTools(), mcp::CapabilityError);
  REQUIRE_THROWS_AS(client->callTool("echo", mcp::jsonrpc::JsonValue::object()), mcp::CapabilityError);
  REQUIRE_THROWS_AS(client->listResources(), mcp::CapabilityError);
  REQUIRE_THROWS_AS(client->readResource("resource://item-0"), mcp::CapabilityError);
  REQUIRE_THROWS_AS(client->listResourceTemplates(), mcp::CapabilityError);
  REQUIRE_THROWS_AS(client->listPrompts(), mcp::CapabilityError);
  REQUIRE_THROWS_AS(client->getPrompt("example", mcp::jsonrpc::JsonValue::object()), mcp::CapabilityError);
}

TEST_CASE("Client roots/list enforces negotiated capability and provider gating", "[client][roots][capabilities]")
{
  SECTION("roots/list returns method not found before initialize when roots are only configured locally")
  {
    auto client = mcp::Client::create();
    auto transport = std::make_shared<RecordingTransport>();
    client->attachTransport(transport);
    client->start();

    mcp::RootsCapability rootsCapability;
    rootsCapability.listChanged = true;
    mcp::ClientInitializeConfiguration configuration;
    configuration.capabilities = mcp::ClientCapabilities(rootsCapability, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
    client->setInitializeConfiguration(std::move(configuration));
    client->setRootsProvider(
      [](const mcp::RootsListContext &) -> std::vector<mcp::RootEntry>
      {
        return {
          mcp::RootEntry {"file:///workspace/project-a", std::optional<std::string> {"Project A"}, std::nullopt},
        };
      });

    mcp::jsonrpc::Request listRootsRequest;
    listRootsRequest.id = std::int64_t {9000};
    listRootsRequest.method = "roots/list";
    listRootsRequest.params = mcp::jsonrpc::JsonValue::object();

    const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, listRootsRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kMethodNotFound));
    REQUIRE(error.error.message == "Roots not supported");
    REQUIRE(error.error.data.has_value());
    if (error.error.data.has_value())
    {
      REQUIRE((*error.error.data)["reason"].as<std::string>() == "Client does not have roots capability");
    }
  }

  SECTION("roots/list returns method not found when roots capability is not negotiated")
  {
    auto client = mcp::Client::create();
    auto transport = std::make_shared<RecordingTransport>();
    client->attachTransport(transport);
    client->start();

    auto initializeFuture = client->initialize();
    const auto outboundMessages = transport->messages();
    REQUIRE(outboundMessages.size() == 1);
    const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
    REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
    static_cast<void>(initializeFuture.get());

    mcp::jsonrpc::Request listRootsRequest;
    listRootsRequest.id = std::int64_t {9001};
    listRootsRequest.method = "roots/list";
    listRootsRequest.params = mcp::jsonrpc::JsonValue::object();

    const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, listRootsRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kMethodNotFound));
    REQUIRE(error.error.message == "Roots not supported");
    REQUIRE(error.error.data.has_value());
    if (error.error.data.has_value())
    {
      REQUIRE((*error.error.data)["reason"].as<std::string>() == "Client does not have roots capability");
    }
  }

  SECTION("roots/list returns method not found when no roots provider is registered")
  {
    auto client = mcp::Client::create();
    auto transport = std::make_shared<RecordingTransport>();
    client->attachTransport(transport);
    client->start();

    mcp::RootsCapability rootsCapability;
    rootsCapability.listChanged = true;
    mcp::ClientInitializeConfiguration configuration;
    configuration.capabilities = mcp::ClientCapabilities(rootsCapability, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
    client->setInitializeConfiguration(std::move(configuration));

    auto initializeFuture = client->initialize();
    const auto outboundMessages = transport->messages();
    REQUIRE(outboundMessages.size() == 1);
    const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
    REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
    static_cast<void>(initializeFuture.get());

    mcp::jsonrpc::Request listRootsRequest;
    listRootsRequest.id = std::int64_t {9002};
    listRootsRequest.method = "roots/list";
    listRootsRequest.params = mcp::jsonrpc::JsonValue::object();

    const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, listRootsRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kMethodNotFound));
    REQUIRE(error.error.message == "Roots not supported");
    REQUIRE(error.error.data.has_value());
    if (error.error.data.has_value())
    {
      REQUIRE((*error.error.data)["reason"].as<std::string>() == "Client does not have a registered roots provider");
    }
  }
}

TEST_CASE("Client roots/list validates root URI scheme", "[client][roots][validation]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::RootsCapability rootsCapability;
  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(rootsCapability, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  client->setInitializeConfiguration(std::move(configuration));

  client->setRootsProvider(
    [](const mcp::RootsListContext &) -> std::vector<mcp::RootEntry>
    {
      return {
        mcp::RootEntry {"https://example.com/not-file", std::optional<std::string> {"invalid"}, std::nullopt},
      };
    });

  auto initializeFuture = client->initialize();
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());

  mcp::jsonrpc::Request listRootsRequest;
  listRootsRequest.id = std::int64_t {9003};
  listRootsRequest.method = "roots/list";

  const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, listRootsRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
  const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
  REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInternalError));
  REQUIRE(error.error.message == "roots/list provider returned invalid root URI; expected file://");
}

TEST_CASE("Client roots/list returns file roots from provider", "[client][roots][handler]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::RootsCapability rootsCapability;
  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(rootsCapability, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  client->setInitializeConfiguration(std::move(configuration));

  client->setRootsProvider(
    [](const mcp::RootsListContext &) -> std::vector<mcp::RootEntry>
    {
      mcp::jsonrpc::JsonValue metadata = mcp::jsonrpc::JsonValue::object();
      metadata["source"] = "unit-test";
      return {
        mcp::RootEntry {"file:///workspace/project-a", std::optional<std::string> {"Project A"}, std::move(metadata)},
        mcp::RootEntry {"file:///workspace/project-b", std::nullopt, std::nullopt},
      };
    });

  auto initializeFuture = client->initialize();
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());

  mcp::jsonrpc::Request listRootsRequest;
  listRootsRequest.id = std::int64_t {9004};
  listRootsRequest.method = "roots/list";

  const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, listRootsRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response));
  const auto &success = std::get<mcp::jsonrpc::SuccessResponse>(response);
  REQUIRE(success.result.contains("roots"));
  REQUIRE(success.result["roots"].is_array());
  REQUIRE(success.result["roots"].size() == 2);
  REQUIRE(success.result["roots"][0]["uri"].as<std::string>() == "file:///workspace/project-a");
  REQUIRE(success.result["roots"][0]["name"].as<std::string>() == "Project A");
  REQUIRE(success.result["roots"][0]["_meta"]["source"].as<std::string>() == "unit-test");
  REQUIRE(success.result["roots"][1]["uri"].as<std::string>() == "file:///workspace/project-b");
}

TEST_CASE("Client notifyRootsListChanged is gated by roots.listChanged", "[client][roots][notifications]")
{
  SECTION("notifyRootsListChanged returns false when listChanged is not enabled")
  {
    auto client = mcp::Client::create();
    auto transport = std::make_shared<RecordingTransport>();
    client->attachTransport(transport);
    client->start();

    mcp::RootsCapability rootsCapability;
    rootsCapability.listChanged = false;
    mcp::ClientInitializeConfiguration configuration;
    configuration.capabilities = mcp::ClientCapabilities(rootsCapability, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
    client->setInitializeConfiguration(std::move(configuration));

    auto initializeFuture = client->initialize();
    const auto outboundMessages = transport->messages();
    REQUIRE(outboundMessages.size() == 1);
    const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
    REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
    static_cast<void>(initializeFuture.get());

    const std::size_t messageCountBeforeNotify = transport->messages().size();
    REQUIRE_FALSE(client->notifyRootsListChanged());
    REQUIRE(transport->messages().size() == messageCountBeforeNotify);
  }

  SECTION("notifyRootsListChanged sends notification when listChanged is enabled")
  {
    auto client = mcp::Client::create();
    auto transport = std::make_shared<RecordingTransport>();
    client->attachTransport(transport);
    client->start();

    mcp::RootsCapability rootsCapability;
    rootsCapability.listChanged = true;
    mcp::ClientInitializeConfiguration configuration;
    configuration.capabilities = mcp::ClientCapabilities(rootsCapability, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
    client->setInitializeConfiguration(std::move(configuration));

    auto initializeFuture = client->initialize();
    const auto outboundMessages = transport->messages();
    REQUIRE(outboundMessages.size() == 1);
    const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
    REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
    static_cast<void>(initializeFuture.get());

    const std::size_t messageCountBeforeNotify = transport->messages().size();
    REQUIRE(client->notifyRootsListChanged());

    const auto finalMessages = transport->messages();
    REQUIRE(finalMessages.size() == messageCountBeforeNotify + 1);
    REQUIRE(std::holds_alternative<mcp::jsonrpc::Notification>(finalMessages.back()));
    const auto &notification = std::get<mcp::jsonrpc::Notification>(finalMessages.back());
    REQUIRE(notification.method == "notifications/roots/list_changed");
  }
}

TEST_CASE("Client sampling/createMessage validates roles and tool capability", "[client][sampling][validation]")
{
  SECTION("sampling/createMessage rejects invalid role")
  {
    auto client = mcp::Client::create();
    auto transport = std::make_shared<RecordingTransport>();
    client->attachTransport(transport);
    client->start();

    mcp::SamplingCapability samplingCapability;
    mcp::ClientInitializeConfiguration configuration;
    configuration.capabilities = mcp::ClientCapabilities(std::nullopt, samplingCapability, std::nullopt, std::nullopt, std::nullopt);
    client->setInitializeConfiguration(std::move(configuration));

    client->setSamplingCreateMessageHandler(
      [](const mcp::SamplingCreateMessageContext &, const mcp::jsonrpc::JsonValue &) -> std::optional<mcp::jsonrpc::JsonValue>
      {
        mcp::jsonrpc::JsonValue result = mcp::jsonrpc::JsonValue::object();
        result["role"] = "assistant";
        result["model"] = "test-model";
        result["content"] = mcp::jsonrpc::JsonValue::object();
        result["content"]["type"] = "text";
        result["content"]["text"] = "ok";
        return result;
      });

    auto initializeFuture = client->initialize();
    const auto outboundMessages = transport->messages();
    REQUIRE(outboundMessages.size() == 1);
    const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
    REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
    static_cast<void>(initializeFuture.get());

    mcp::jsonrpc::Request samplingRequest;
    samplingRequest.id = std::int64_t {9101};
    samplingRequest.method = "sampling/createMessage";
    samplingRequest.params = mcp::jsonrpc::JsonValue::object();
    (*samplingRequest.params)["maxTokens"] = 64;
    (*samplingRequest.params)["messages"] = mcp::jsonrpc::JsonValue::array();
    mcp::jsonrpc::JsonValue message = mcp::jsonrpc::JsonValue::object();
    message["role"] = "system";
    message["content"] = mcp::jsonrpc::JsonValue::object();
    message["content"]["type"] = "text";
    message["content"]["text"] = "bad role";
    (*samplingRequest.params)["messages"].push_back(std::move(message));

    const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, samplingRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams));
  }

  SECTION("sampling/createMessage rejects tools fields without sampling.tools")
  {
    auto client = mcp::Client::create();
    auto transport = std::make_shared<RecordingTransport>();
    client->attachTransport(transport);
    client->start();

    mcp::SamplingCapability samplingCapability;
    samplingCapability.tools = false;
    mcp::ClientInitializeConfiguration configuration;
    configuration.capabilities = mcp::ClientCapabilities(std::nullopt, samplingCapability, std::nullopt, std::nullopt, std::nullopt);
    client->setInitializeConfiguration(std::move(configuration));

    client->setSamplingCreateMessageHandler(
      [](const mcp::SamplingCreateMessageContext &, const mcp::jsonrpc::JsonValue &) -> std::optional<mcp::jsonrpc::JsonValue>
      {
        mcp::jsonrpc::JsonValue result = mcp::jsonrpc::JsonValue::object();
        result["role"] = "assistant";
        result["model"] = "test-model";
        result["content"] = mcp::jsonrpc::JsonValue::object();
        result["content"]["type"] = "text";
        result["content"]["text"] = "ok";
        return result;
      });

    auto initializeFuture = client->initialize();
    const auto outboundMessages = transport->messages();
    REQUIRE(outboundMessages.size() == 1);
    const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
    REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
    static_cast<void>(initializeFuture.get());

    mcp::jsonrpc::Request samplingRequest;
    samplingRequest.id = std::int64_t {9102};
    samplingRequest.method = "sampling/createMessage";
    samplingRequest.params = mcp::jsonrpc::JsonValue::object();
    (*samplingRequest.params)["maxTokens"] = 64;
    (*samplingRequest.params)["messages"] = mcp::jsonrpc::JsonValue::array();
    mcp::jsonrpc::JsonValue message = mcp::jsonrpc::JsonValue::object();
    message["role"] = "user";
    message["content"] = mcp::jsonrpc::JsonValue::object();
    message["content"]["type"] = "text";
    message["content"]["text"] = "hello";
    (*samplingRequest.params)["messages"].push_back(std::move(message));
    (*samplingRequest.params)["toolChoice"] = mcp::jsonrpc::JsonValue::object();
    (*samplingRequest.params)["toolChoice"]["mode"] = "auto";

    const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, samplingRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams));
  }

  SECTION("sampling/createMessage rejects mixed user content when tool_result is present")
  {
    auto client = mcp::Client::create();
    auto transport = std::make_shared<RecordingTransport>();
    client->attachTransport(transport);
    client->start();

    mcp::SamplingCapability samplingCapability;
    mcp::ClientInitializeConfiguration configuration;
    configuration.capabilities = mcp::ClientCapabilities(std::nullopt, samplingCapability, std::nullopt, std::nullopt, std::nullopt);
    client->setInitializeConfiguration(std::move(configuration));

    client->setSamplingCreateMessageHandler(
      [](const mcp::SamplingCreateMessageContext &, const mcp::jsonrpc::JsonValue &) -> std::optional<mcp::jsonrpc::JsonValue>
      {
        mcp::jsonrpc::JsonValue result = mcp::jsonrpc::JsonValue::object();
        result["role"] = "assistant";
        result["model"] = "test-model";
        result["content"] = mcp::jsonrpc::JsonValue::object();
        result["content"]["type"] = "text";
        result["content"]["text"] = "ok";
        return result;
      });

    auto initializeFuture = client->initialize();
    const auto outboundMessages = transport->messages();
    REQUIRE(outboundMessages.size() == 1);
    const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
    REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
    static_cast<void>(initializeFuture.get());

    mcp::jsonrpc::Request samplingRequest;
    samplingRequest.id = std::int64_t {9105};
    samplingRequest.method = "sampling/createMessage";
    samplingRequest.params = mcp::jsonrpc::JsonValue::object();
    (*samplingRequest.params)["maxTokens"] = 64;
    (*samplingRequest.params)["messages"] = mcp::jsonrpc::JsonValue::array();

    mcp::jsonrpc::JsonValue firstMessage = mcp::jsonrpc::JsonValue::object();
    firstMessage["role"] = "user";
    firstMessage["content"] = mcp::jsonrpc::JsonValue::object();
    firstMessage["content"]["type"] = "text";
    firstMessage["content"]["text"] = "look up weather";
    (*samplingRequest.params)["messages"].push_back(std::move(firstMessage));

    mcp::jsonrpc::JsonValue assistantToolUseMessage = mcp::jsonrpc::JsonValue::object();
    assistantToolUseMessage["role"] = "assistant";
    assistantToolUseMessage["content"] = mcp::jsonrpc::JsonValue::array();

    mcp::jsonrpc::JsonValue toolUse = mcp::jsonrpc::JsonValue::object();
    toolUse["type"] = "tool_use";
    toolUse["id"] = "call-1";
    toolUse["name"] = "lookup_weather";
    toolUse["input"] = mcp::jsonrpc::JsonValue::object();
    assistantToolUseMessage["content"].push_back(std::move(toolUse));
    (*samplingRequest.params)["messages"].push_back(std::move(assistantToolUseMessage));

    mcp::jsonrpc::JsonValue invalidToolResultMessage = mcp::jsonrpc::JsonValue::object();
    invalidToolResultMessage["role"] = "user";
    invalidToolResultMessage["content"] = mcp::jsonrpc::JsonValue::array();

    mcp::jsonrpc::JsonValue textBlock = mcp::jsonrpc::JsonValue::object();
    textBlock["type"] = "text";
    textBlock["text"] = "mixing content types";
    invalidToolResultMessage["content"].push_back(std::move(textBlock));

    mcp::jsonrpc::JsonValue toolResult = mcp::jsonrpc::JsonValue::object();
    toolResult["type"] = "tool_result";
    toolResult["toolUseId"] = "call-1";
    toolResult["content"] = mcp::jsonrpc::JsonValue::array();
    mcp::jsonrpc::JsonValue resultContent = mcp::jsonrpc::JsonValue::object();
    resultContent["type"] = "text";
    resultContent["text"] = "sunny";
    toolResult["content"].push_back(std::move(resultContent));
    invalidToolResultMessage["content"].push_back(std::move(toolResult));

    (*samplingRequest.params)["messages"].push_back(std::move(invalidToolResultMessage));

    const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, samplingRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams));
    REQUIRE(error.error.message == "sampling/createMessage user messages with tool_result must contain only tool_result blocks");
  }

  SECTION("sampling/createMessage rejects unbalanced tool_use and tool_result IDs")
  {
    auto client = mcp::Client::create();
    auto transport = std::make_shared<RecordingTransport>();
    client->attachTransport(transport);
    client->start();

    mcp::SamplingCapability samplingCapability;
    mcp::ClientInitializeConfiguration configuration;
    configuration.capabilities = mcp::ClientCapabilities(std::nullopt, samplingCapability, std::nullopt, std::nullopt, std::nullopt);
    client->setInitializeConfiguration(std::move(configuration));

    client->setSamplingCreateMessageHandler(
      [](const mcp::SamplingCreateMessageContext &, const mcp::jsonrpc::JsonValue &) -> std::optional<mcp::jsonrpc::JsonValue>
      {
        mcp::jsonrpc::JsonValue result = mcp::jsonrpc::JsonValue::object();
        result["role"] = "assistant";
        result["model"] = "test-model";
        result["content"] = mcp::jsonrpc::JsonValue::object();
        result["content"]["type"] = "text";
        result["content"]["text"] = "ok";
        return result;
      });

    auto initializeFuture = client->initialize();
    const auto outboundMessages = transport->messages();
    REQUIRE(outboundMessages.size() == 1);
    const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
    REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
    static_cast<void>(initializeFuture.get());

    mcp::jsonrpc::Request samplingRequest;
    samplingRequest.id = std::int64_t {9106};
    samplingRequest.method = "sampling/createMessage";
    samplingRequest.params = mcp::jsonrpc::JsonValue::object();
    (*samplingRequest.params)["maxTokens"] = 64;
    (*samplingRequest.params)["messages"] = mcp::jsonrpc::JsonValue::array();

    mcp::jsonrpc::JsonValue firstMessage = mcp::jsonrpc::JsonValue::object();
    firstMessage["role"] = "user";
    firstMessage["content"] = mcp::jsonrpc::JsonValue::object();
    firstMessage["content"]["type"] = "text";
    firstMessage["content"]["text"] = "look up weather";
    (*samplingRequest.params)["messages"].push_back(std::move(firstMessage));

    mcp::jsonrpc::JsonValue assistantToolUseMessage = mcp::jsonrpc::JsonValue::object();
    assistantToolUseMessage["role"] = "assistant";
    assistantToolUseMessage["content"] = mcp::jsonrpc::JsonValue::array();

    mcp::jsonrpc::JsonValue toolUseA = mcp::jsonrpc::JsonValue::object();
    toolUseA["type"] = "tool_use";
    toolUseA["id"] = "call-a";
    toolUseA["name"] = "lookup_weather";
    toolUseA["input"] = mcp::jsonrpc::JsonValue::object();
    assistantToolUseMessage["content"].push_back(std::move(toolUseA));

    mcp::jsonrpc::JsonValue toolUseB = mcp::jsonrpc::JsonValue::object();
    toolUseB["type"] = "tool_use";
    toolUseB["id"] = "call-b";
    toolUseB["name"] = "lookup_weather";
    toolUseB["input"] = mcp::jsonrpc::JsonValue::object();
    assistantToolUseMessage["content"].push_back(std::move(toolUseB));

    (*samplingRequest.params)["messages"].push_back(std::move(assistantToolUseMessage));

    mcp::jsonrpc::JsonValue incompleteToolResultMessage = mcp::jsonrpc::JsonValue::object();
    incompleteToolResultMessage["role"] = "user";
    incompleteToolResultMessage["content"] = mcp::jsonrpc::JsonValue::array();

    mcp::jsonrpc::JsonValue toolResult = mcp::jsonrpc::JsonValue::object();
    toolResult["type"] = "tool_result";
    toolResult["toolUseId"] = "call-a";
    toolResult["content"] = mcp::jsonrpc::JsonValue::array();
    mcp::jsonrpc::JsonValue resultContent = mcp::jsonrpc::JsonValue::object();
    resultContent["type"] = "text";
    resultContent["text"] = "sunny";
    toolResult["content"].push_back(std::move(resultContent));
    incompleteToolResultMessage["content"].push_back(std::move(toolResult));

    (*samplingRequest.params)["messages"].push_back(std::move(incompleteToolResultMessage));

    const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, samplingRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams));
    REQUIRE(error.error.message == "sampling/createMessage tool_use and tool_result blocks must be balanced in sequence");
  }
}

TEST_CASE("Client sampling/createMessage happy path and user rejection", "[client][sampling][handler]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::SamplingCapability samplingCapability;
  samplingCapability.tools = true;
  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(std::nullopt, samplingCapability, std::nullopt, std::nullopt, std::nullopt);
  client->setInitializeConfiguration(std::move(configuration));

  auto initializeFuture = client->initialize();
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());

  client->setSamplingCreateMessageHandler(
    [](const mcp::SamplingCreateMessageContext &, const mcp::jsonrpc::JsonValue &params) -> std::optional<mcp::jsonrpc::JsonValue>
    {
      mcp::jsonrpc::JsonValue result = mcp::jsonrpc::JsonValue::object();
      result["role"] = "assistant";
      result["model"] = "test-model";
      result["content"] = mcp::jsonrpc::JsonValue::object();
      result["content"]["type"] = "text";
      result["content"]["text"] = "Echo: " + params["messages"][0]["content"]["text"].as<std::string>();
      result["stopReason"] = "endTurn";
      return result;
    });

  mcp::jsonrpc::Request samplingRequest;
  samplingRequest.id = std::int64_t {9103};
  samplingRequest.method = "sampling/createMessage";
  samplingRequest.params = mcp::jsonrpc::JsonValue::object();
  (*samplingRequest.params)["maxTokens"] = 64;
  (*samplingRequest.params)["messages"] = mcp::jsonrpc::JsonValue::array();
  mcp::jsonrpc::JsonValue message = mcp::jsonrpc::JsonValue::object();
  message["role"] = "user";
  message["content"] = mcp::jsonrpc::JsonValue::object();
  message["content"]["type"] = "text";
  message["content"]["text"] = "hello";
  (*samplingRequest.params)["messages"].push_back(std::move(message));

  const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, samplingRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response));
  const auto &success = std::get<mcp::jsonrpc::SuccessResponse>(response);
  REQUIRE(success.result["role"].as<std::string>() == "assistant");
  REQUIRE(success.result["model"].as<std::string>() == "test-model");
  REQUIRE(success.result["content"]["type"].as<std::string>() == "text");
  REQUIRE(success.result["content"]["text"].as<std::string>() == "Echo: hello");

  client->setSamplingCreateMessageHandler([](const mcp::SamplingCreateMessageContext &, const mcp::jsonrpc::JsonValue &) -> std::optional<mcp::jsonrpc::JsonValue>
                                          { return std::nullopt; });

  mcp::jsonrpc::Request rejectedRequest = samplingRequest;
  rejectedRequest.id = std::int64_t {9104};
  const mcp::jsonrpc::Response rejectedResponse = client->handleRequest(mcp::jsonrpc::RequestContext {}, rejectedRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(rejectedResponse));
  const auto &rejectedError = std::get<mcp::jsonrpc::ErrorResponse>(rejectedResponse);
  REQUIRE(rejectedError.error.code == -1);
}

TEST_CASE("Client sampling/createMessage supports task augmentation with deferred tasks/result", "[client][tasks][sampling]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::SamplingCapability samplingCapability;
  samplingCapability.tools = true;

  mcp::TasksCapability tasksCapability;
  tasksCapability.list = true;
  tasksCapability.cancel = true;
  tasksCapability.samplingCreateMessage = true;

  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(std::nullopt, samplingCapability, std::nullopt, tasksCapability, std::nullopt);
  client->setInitializeConfiguration(std::move(configuration));

  client->setSamplingCreateMessageHandler(
    [](const mcp::SamplingCreateMessageContext &, const mcp::jsonrpc::JsonValue &) -> std::optional<mcp::jsonrpc::JsonValue>
    {
      mcp::jsonrpc::JsonValue result = mcp::jsonrpc::JsonValue::object();
      result["role"] = "assistant";
      result["model"] = "task-model";
      result["content"] = mcp::jsonrpc::JsonValue::object();
      result["content"]["type"] = "text";
      result["content"]["text"] = "completed";
      return result;
    });

  auto initializeFuture = client->initialize();
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());

  mcp::jsonrpc::RequestContext authA;
  authA.authContext = "auth-a";

  mcp::jsonrpc::Request samplingRequest;
  samplingRequest.id = std::int64_t {9301};
  samplingRequest.method = "sampling/createMessage";
  samplingRequest.params = mcp::jsonrpc::JsonValue::object();
  (*samplingRequest.params)["maxTokens"] = 32;
  (*samplingRequest.params)["messages"] = mcp::jsonrpc::JsonValue::array();
  mcp::jsonrpc::JsonValue message = mcp::jsonrpc::JsonValue::object();
  message["role"] = "user";
  message["content"] = mcp::jsonrpc::JsonValue::object();
  message["content"]["type"] = "text";
  message["content"]["text"] = "hello";
  (*samplingRequest.params)["messages"].push_back(std::move(message));
  (*samplingRequest.params)["task"] = mcp::jsonrpc::JsonValue::object();
  (*samplingRequest.params)["task"]["ttl"] = std::int64_t {5000};

  const mcp::jsonrpc::Response taskCreateResponse = client->handleRequest(authA, samplingRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(taskCreateResponse));
  const auto &taskCreate = std::get<mcp::jsonrpc::SuccessResponse>(taskCreateResponse);
  REQUIRE(taskCreate.result.contains("task"));
  const std::string taskId = taskCreate.result["task"]["taskId"].as<std::string>();
  REQUIRE_FALSE(taskId.empty());

  mcp::jsonrpc::Request resultRequest;
  resultRequest.id = std::int64_t {9302};
  resultRequest.method = "tasks/result";
  resultRequest.params = mcp::jsonrpc::JsonValue::object();
  (*resultRequest.params)["taskId"] = taskId;

  const mcp::jsonrpc::Response taskResultResponse = client->handleRequest(authA, resultRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(taskResultResponse));
  const auto &taskResult = std::get<mcp::jsonrpc::SuccessResponse>(taskResultResponse);
  REQUIRE(taskResult.result["role"].as<std::string>() == "assistant");
  REQUIRE(taskResult.result["model"].as<std::string>() == "task-model");
  REQUIRE(taskResult.result["content"]["text"].as<std::string>() == "completed");
  REQUIRE(taskResult.result["_meta"]["io.modelcontextprotocol/related-task"]["taskId"].as<std::string>() == taskId);

  mcp::jsonrpc::RequestContext authB;
  authB.authContext = "auth-b";

  mcp::jsonrpc::Request deniedGet;
  deniedGet.id = std::int64_t {9303};
  deniedGet.method = "tasks/get";
  deniedGet.params = mcp::jsonrpc::JsonValue::object();
  (*deniedGet.params)["taskId"] = taskId;

  const mcp::jsonrpc::Response deniedGetResponse = client->handleRequest(authB, deniedGet).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(deniedGetResponse));
  const auto &deniedGetError = std::get<mcp::jsonrpc::ErrorResponse>(deniedGetResponse);
  REQUIRE(deniedGetError.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams));
}

TEST_CASE("Client sampling/createMessage ignores task metadata when tasks capability is not negotiated", "[client][tasks][sampling]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::SamplingCapability samplingCapability;
  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(std::nullopt, samplingCapability, std::nullopt, std::nullopt, std::nullopt);
  client->setInitializeConfiguration(std::move(configuration));

  client->setSamplingCreateMessageHandler(
    [](const mcp::SamplingCreateMessageContext &, const mcp::jsonrpc::JsonValue &) -> std::optional<mcp::jsonrpc::JsonValue>
    {
      mcp::jsonrpc::JsonValue result = mcp::jsonrpc::JsonValue::object();
      result["role"] = "assistant";
      result["model"] = "no-task-model";
      result["content"] = mcp::jsonrpc::JsonValue::object();
      result["content"]["type"] = "text";
      result["content"]["text"] = "direct";
      return result;
    });

  auto initializeFuture = client->initialize();
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());

  mcp::jsonrpc::Request samplingRequest;
  samplingRequest.id = std::int64_t {9304};
  samplingRequest.method = "sampling/createMessage";
  samplingRequest.params = mcp::jsonrpc::JsonValue::object();
  (*samplingRequest.params)["maxTokens"] = 32;
  (*samplingRequest.params)["messages"] = mcp::jsonrpc::JsonValue::array();
  mcp::jsonrpc::JsonValue message = mcp::jsonrpc::JsonValue::object();
  message["role"] = "user";
  message["content"] = mcp::jsonrpc::JsonValue::object();
  message["content"]["type"] = "text";
  message["content"]["text"] = "hello";
  (*samplingRequest.params)["messages"].push_back(std::move(message));
  (*samplingRequest.params)["task"] = mcp::jsonrpc::JsonValue::object();

  const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, samplingRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response));
  const auto &success = std::get<mcp::jsonrpc::SuccessResponse>(response);
  REQUIRE(success.result["model"].as<std::string>() == "no-task-model");
  REQUIRE_FALSE(success.result.contains("task"));
}

TEST_CASE("Client elicitation/create supports task augmentation", "[client][tasks][elicitation]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::ElicitationCapability elicitationCapability;
  elicitationCapability.form = true;

  mcp::TasksCapability tasksCapability;
  tasksCapability.elicitationCreate = true;

  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(std::nullopt, std::nullopt, elicitationCapability, tasksCapability, std::nullopt);
  client->setInitializeConfiguration(std::move(configuration));

  client->setFormElicitationHandler(
    [](const mcp::ElicitationCreateContext &, const mcp::FormElicitationRequest &) -> mcp::FormElicitationResult
    {
      mcp::FormElicitationResult result;
      result.action = mcp::ElicitationAction::kAccept;
      result.content = mcp::jsonrpc::JsonValue::object();
      (*result.content)["name"] = "octocat";
      return result;
    });

  auto initializeFuture = client->initialize();
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());

  mcp::jsonrpc::RequestContext authA;
  authA.authContext = "auth-a";

  mcp::jsonrpc::Request elicitationRequest;
  elicitationRequest.id = std::int64_t {9305};
  elicitationRequest.method = "elicitation/create";
  elicitationRequest.params = mcp::jsonrpc::JsonValue::object();
  (*elicitationRequest.params)["mode"] = "form";
  (*elicitationRequest.params)["message"] = "Collect project name";
  (*elicitationRequest.params)["requestedSchema"] = mcp::jsonrpc::JsonValue::object();
  (*elicitationRequest.params)["requestedSchema"]["type"] = "object";
  (*elicitationRequest.params)["requestedSchema"]["properties"] = mcp::jsonrpc::JsonValue::object();
  (*elicitationRequest.params)["requestedSchema"]["properties"]["name"] = mcp::jsonrpc::JsonValue::object();
  (*elicitationRequest.params)["requestedSchema"]["properties"]["name"]["type"] = "string";
  (*elicitationRequest.params)["task"] = mcp::jsonrpc::JsonValue::object();

  const mcp::jsonrpc::Response taskCreateResponse = client->handleRequest(authA, elicitationRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(taskCreateResponse));
  const auto &taskCreate = std::get<mcp::jsonrpc::SuccessResponse>(taskCreateResponse);
  REQUIRE(taskCreate.result.contains("task"));
  const std::string taskId = taskCreate.result["task"]["taskId"].as<std::string>();
  REQUIRE_FALSE(taskId.empty());

  mcp::jsonrpc::Request resultRequest;
  resultRequest.id = std::int64_t {9306};
  resultRequest.method = "tasks/result";
  resultRequest.params = mcp::jsonrpc::JsonValue::object();
  (*resultRequest.params)["taskId"] = taskId;

  const mcp::jsonrpc::Response taskResultResponse = client->handleRequest(authA, resultRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(taskResultResponse));
  const auto &taskResult = std::get<mcp::jsonrpc::SuccessResponse>(taskResultResponse);
  REQUIRE(taskResult.result["action"].as<std::string>() == "accept");
  REQUIRE(taskResult.result["content"]["name"].as<std::string>() == "octocat");
  REQUIRE(taskResult.result["_meta"]["io.modelcontextprotocol/related-task"]["taskId"].as<std::string>() == taskId);
}

TEST_CASE("Client teardown is safe with in-flight task-augmented elicitation worker", "[client][tasks][elicitation][teardown]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::ElicitationCapability elicitationCapability;
  elicitationCapability.form = true;

  mcp::TasksCapability tasksCapability;
  tasksCapability.elicitationCreate = true;

  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(std::nullopt, std::nullopt, elicitationCapability, tasksCapability, std::nullopt);
  client->setInitializeConfiguration(std::move(configuration));

  std::promise<void> handlerEnteredPromise;
  auto handlerEnteredFuture = handlerEnteredPromise.get_future();
  std::promise<void> releaseHandlerPromise;
  auto releaseHandlerFuture = releaseHandlerPromise.get_future();
  std::promise<void> handlerFinishedPromise;
  auto handlerFinishedFuture = handlerFinishedPromise.get_future();

  client->setFormElicitationHandler(
    [&handlerEnteredPromise, &releaseHandlerFuture, &handlerFinishedPromise](const mcp::ElicitationCreateContext &,
                                                                             const mcp::FormElicitationRequest &) -> mcp::FormElicitationResult
    {
      handlerEnteredPromise.set_value();
      releaseHandlerFuture.wait();

      mcp::FormElicitationResult result;
      result.action = mcp::ElicitationAction::kCancel;
      handlerFinishedPromise.set_value();
      return result;
    });

  auto initializeFuture = client->initialize();
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());

  const std::size_t messageCountBeforeTask = transport->messages().size();
  std::shared_ptr<mcp::Session> retainedSession = client->session();

  mcp::jsonrpc::Request request;
  request.id = std::int64_t {9312};
  request.method = "elicitation/create";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["mode"] = "form";
  (*request.params)["message"] = "long-running";
  (*request.params)["requestedSchema"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["requestedSchema"]["type"] = "object";
  (*request.params)["requestedSchema"]["properties"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["requestedSchema"]["properties"]["name"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["requestedSchema"]["properties"]["name"]["type"] = "string";
  (*request.params)["task"] = mcp::jsonrpc::JsonValue::object();

  const mcp::jsonrpc::Response taskCreateResponse = client->handleRequest(mcp::jsonrpc::RequestContext {}, request).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(taskCreateResponse));
  const auto &taskCreate = std::get<mcp::jsonrpc::SuccessResponse>(taskCreateResponse);
  REQUIRE(taskCreate.result.contains("task"));
  const std::string taskId = taskCreate.result["task"]["taskId"].as<std::string>();
  REQUIRE(handlerEnteredFuture.wait_for(std::chrono::seconds(1)) == std::future_status::ready);

  std::future<void> destroyFuture = std::async(std::launch::async, [client = std::move(client)]() mutable -> void { client.reset(); });

  releaseHandlerPromise.set_value();
  REQUIRE(handlerFinishedFuture.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
  REQUIRE(destroyFuture.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
  destroyFuture.get();

  static_cast<void>(messageCountBeforeTask);
  static_cast<void>(taskId);
  retainedSession.reset();
}

TEST_CASE("Client tasks/list and tasks/cancel are gated by negotiated sub-capabilities", "[client][tasks][capabilities]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::SamplingCapability samplingCapability;

  mcp::TasksCapability tasksCapability;
  tasksCapability.list = false;
  tasksCapability.cancel = false;
  tasksCapability.samplingCreateMessage = true;

  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(std::nullopt, samplingCapability, std::nullopt, tasksCapability, std::nullopt);
  client->setInitializeConfiguration(std::move(configuration));

  client->setSamplingCreateMessageHandler(
    [](const mcp::SamplingCreateMessageContext &, const mcp::jsonrpc::JsonValue &) -> std::optional<mcp::jsonrpc::JsonValue>
    {
      mcp::jsonrpc::JsonValue result = mcp::jsonrpc::JsonValue::object();
      result["role"] = "assistant";
      result["model"] = "task-model";
      result["content"] = mcp::jsonrpc::JsonValue::object();
      result["content"]["type"] = "text";
      result["content"]["text"] = "ok";
      return result;
    });

  auto initializeFuture = client->initialize();
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());

  mcp::jsonrpc::Request createRequest;
  createRequest.id = std::int64_t {9307};
  createRequest.method = "sampling/createMessage";
  createRequest.params = mcp::jsonrpc::JsonValue::object();
  (*createRequest.params)["maxTokens"] = 16;
  (*createRequest.params)["messages"] = mcp::jsonrpc::JsonValue::array();
  mcp::jsonrpc::JsonValue message = mcp::jsonrpc::JsonValue::object();
  message["role"] = "user";
  message["content"] = mcp::jsonrpc::JsonValue::object();
  message["content"]["type"] = "text";
  message["content"]["text"] = "ping";
  (*createRequest.params)["messages"].push_back(std::move(message));
  (*createRequest.params)["task"] = mcp::jsonrpc::JsonValue::object();

  const mcp::jsonrpc::Response createResponse = client->handleRequest(mcp::jsonrpc::RequestContext {}, createRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(createResponse));
  const std::string taskId = std::get<mcp::jsonrpc::SuccessResponse>(createResponse).result["task"]["taskId"].as<std::string>();

  mcp::jsonrpc::Request listRequest;
  listRequest.id = std::int64_t {9308};
  listRequest.method = "tasks/list";
  listRequest.params = mcp::jsonrpc::JsonValue::object();
  const mcp::jsonrpc::Response listResponse = client->handleRequest(mcp::jsonrpc::RequestContext {}, listRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(listResponse));
  REQUIRE(std::get<mcp::jsonrpc::ErrorResponse>(listResponse).error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kMethodNotFound));

  mcp::jsonrpc::Request cancelRequest;
  cancelRequest.id = std::int64_t {9309};
  cancelRequest.method = "tasks/cancel";
  cancelRequest.params = mcp::jsonrpc::JsonValue::object();
  (*cancelRequest.params)["taskId"] = taskId;
  const mcp::jsonrpc::Response cancelResponse = client->handleRequest(mcp::jsonrpc::RequestContext {}, cancelRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(cancelResponse));
  REQUIRE(std::get<mcp::jsonrpc::ErrorResponse>(cancelResponse).error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kMethodNotFound));

  mcp::jsonrpc::Request getRequest;
  getRequest.id = std::int64_t {9310};
  getRequest.method = "tasks/get";
  getRequest.params = mcp::jsonrpc::JsonValue::object();
  (*getRequest.params)["taskId"] = taskId;
  const mcp::jsonrpc::Response getResponse = client->handleRequest(mcp::jsonrpc::RequestContext {}, getRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(getResponse));

  mcp::jsonrpc::Request resultRequest;
  resultRequest.id = std::int64_t {9311};
  resultRequest.method = "tasks/result";
  resultRequest.params = mcp::jsonrpc::JsonValue::object();
  (*resultRequest.params)["taskId"] = taskId;
  const mcp::jsonrpc::Response resultResponse = client->handleRequest(mcp::jsonrpc::RequestContext {}, resultRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(resultResponse));
}

TEST_CASE("Client elicitation/create enforces capability and mode gating", "[client][elicitation][capabilities]")
{
  SECTION("elicitation/create returns method-not-found when elicitation capability is not negotiated")
  {
    auto client = mcp::Client::create();
    auto transport = std::make_shared<RecordingTransport>();
    client->attachTransport(transport);
    client->start();

    auto initializeFuture = client->initialize();
    const auto outboundMessages = transport->messages();
    REQUIRE(outboundMessages.size() == 1);
    const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
    REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
    static_cast<void>(initializeFuture.get());

    mcp::jsonrpc::Request elicitationRequest;
    elicitationRequest.id = std::int64_t {9201};
    elicitationRequest.method = "elicitation/create";
    elicitationRequest.params = mcp::jsonrpc::JsonValue::object();
    (*elicitationRequest.params)["message"] = "Collect project name";
    (*elicitationRequest.params)["requestedSchema"] = mcp::jsonrpc::JsonValue::object();
    (*elicitationRequest.params)["requestedSchema"]["type"] = "object";
    (*elicitationRequest.params)["requestedSchema"]["properties"] = mcp::jsonrpc::JsonValue::object();
    (*elicitationRequest.params)["requestedSchema"]["properties"]["name"] = mcp::jsonrpc::JsonValue::object();
    (*elicitationRequest.params)["requestedSchema"]["properties"]["name"]["type"] = "string";

    const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, elicitationRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kMethodNotFound));
    REQUIRE(error.error.message == "Elicitation not supported");
  }

  SECTION("elicitation/create rejects mode that was not declared by client")
  {
    auto client = mcp::Client::create();
    auto transport = std::make_shared<RecordingTransport>();
    client->attachTransport(transport);
    client->start();

    mcp::ElicitationCapability elicitationCapability;
    elicitationCapability.form = true;

    mcp::ClientInitializeConfiguration configuration;
    configuration.capabilities = mcp::ClientCapabilities(std::nullopt, std::nullopt, elicitationCapability, std::nullopt, std::nullopt);
    client->setInitializeConfiguration(std::move(configuration));

    client->setFormElicitationHandler(
      [](const mcp::ElicitationCreateContext &, const mcp::FormElicitationRequest &) -> mcp::FormElicitationResult
      {
        mcp::FormElicitationResult result;
        result.action = mcp::ElicitationAction::kCancel;
        return result;
      });

    auto initializeFuture = client->initialize();
    const auto outboundMessages = transport->messages();
    REQUIRE(outboundMessages.size() == 1);
    const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
    REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
    static_cast<void>(initializeFuture.get());

    mcp::jsonrpc::Request elicitationRequest;
    elicitationRequest.id = std::int64_t {9202};
    elicitationRequest.method = "elicitation/create";
    elicitationRequest.params = mcp::jsonrpc::JsonValue::object();
    (*elicitationRequest.params)["mode"] = "url";
    (*elicitationRequest.params)["elicitationId"] = "elic-1";
    (*elicitationRequest.params)["message"] = "Open settings";
    (*elicitationRequest.params)["url"] = "https://example.com/settings";

    const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, elicitationRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams));
  }
}

TEST_CASE("Client elicitation/create supports accept/decline/cancel action model", "[client][elicitation][actions]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::ElicitationCapability elicitationCapability;
  elicitationCapability.form = true;
  elicitationCapability.url = true;

  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(std::nullopt, std::nullopt, elicitationCapability, std::nullopt, std::nullopt);
  client->setInitializeConfiguration(std::move(configuration));

  client->setFormElicitationHandler(
    [](const mcp::ElicitationCreateContext &, const mcp::FormElicitationRequest &request) -> mcp::FormElicitationResult
    {
      mcp::FormElicitationResult result;
      if (request.message == "accept")
      {
        result.action = mcp::ElicitationAction::kAccept;
        result.content = mcp::jsonrpc::JsonValue::object();
        (*result.content)["name"] = "octocat";
        return result;
      }

      if (request.message == "decline")
      {
        result.action = mcp::ElicitationAction::kDecline;
        return result;
      }

      result.action = mcp::ElicitationAction::kCancel;
      return result;
    });

  client->setUrlElicitationHandler(
    [](const mcp::ElicitationCreateContext &, const mcp::UrlElicitationRequest &request) -> mcp::UrlElicitationResult
    {
      mcp::UrlElicitationResult result;
      if (request.elicitationId == "url-accept")
      {
        result.action = mcp::ElicitationAction::kAccept;
      }
      else if (request.elicitationId == "url-decline")
      {
        result.action = mcp::ElicitationAction::kDecline;
      }
      else
      {
        result.action = mcp::ElicitationAction::kCancel;
      }

      return result;
    });

  auto initializeFuture = client->initialize();
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());

  auto makeFormRequest = [](std::int64_t id, std::string message) -> mcp::jsonrpc::Request
  {
    mcp::jsonrpc::Request request;
    request.id = id;
    request.method = "elicitation/create";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["mode"] = "form";
    (*request.params)["message"] = std::move(message);
    (*request.params)["requestedSchema"] = mcp::jsonrpc::JsonValue::object();
    (*request.params)["requestedSchema"]["type"] = "object";
    (*request.params)["requestedSchema"]["properties"] = mcp::jsonrpc::JsonValue::object();
    (*request.params)["requestedSchema"]["properties"]["name"] = mcp::jsonrpc::JsonValue::object();
    (*request.params)["requestedSchema"]["properties"]["name"]["type"] = "string";
    return request;
  };

  const auto formAccept = client->handleRequest(mcp::jsonrpc::RequestContext {}, makeFormRequest(9210, "accept")).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(formAccept));
  const auto &formAcceptResult = std::get<mcp::jsonrpc::SuccessResponse>(formAccept).result;
  REQUIRE(formAcceptResult["action"].as<std::string>() == "accept");
  REQUIRE(formAcceptResult.contains("content"));

  const auto formDecline = client->handleRequest(mcp::jsonrpc::RequestContext {}, makeFormRequest(9211, "decline")).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(formDecline));
  const auto &formDeclineResult = std::get<mcp::jsonrpc::SuccessResponse>(formDecline).result;
  REQUIRE(formDeclineResult["action"].as<std::string>() == "decline");
  REQUIRE_FALSE(formDeclineResult.contains("content"));

  const auto formCancel = client->handleRequest(mcp::jsonrpc::RequestContext {}, makeFormRequest(9212, "cancel")).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(formCancel));
  const auto &formCancelResult = std::get<mcp::jsonrpc::SuccessResponse>(formCancel).result;
  REQUIRE(formCancelResult["action"].as<std::string>() == "cancel");
  REQUIRE_FALSE(formCancelResult.contains("content"));

  auto makeUrlRequest = [](std::int64_t id, std::string elicitationId) -> mcp::jsonrpc::Request
  {
    mcp::jsonrpc::Request request;
    request.id = id;
    request.method = "elicitation/create";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["mode"] = "url";
    (*request.params)["elicitationId"] = std::move(elicitationId);
    (*request.params)["message"] = "Open consent page";
    (*request.params)["url"] = "https://example.com/connect";
    return request;
  };

  const auto urlAccept = client->handleRequest(mcp::jsonrpc::RequestContext {}, makeUrlRequest(9220, "url-accept")).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(urlAccept));
  const auto &urlAcceptResult = std::get<mcp::jsonrpc::SuccessResponse>(urlAccept).result;
  REQUIRE(urlAcceptResult["action"].as<std::string>() == "accept");
  REQUIRE_FALSE(urlAcceptResult.contains("content"));

  const auto urlDecline = client->handleRequest(mcp::jsonrpc::RequestContext {}, makeUrlRequest(9221, "url-decline")).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(urlDecline));
  const auto &urlDeclineResult = std::get<mcp::jsonrpc::SuccessResponse>(urlDecline).result;
  REQUIRE(urlDeclineResult["action"].as<std::string>() == "decline");
  REQUIRE_FALSE(urlDeclineResult.contains("content"));

  const auto urlCancel = client->handleRequest(mcp::jsonrpc::RequestContext {}, makeUrlRequest(9222, "url-cancel")).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(urlCancel));
  const auto &urlCancelResult = std::get<mcp::jsonrpc::SuccessResponse>(urlCancel).result;
  REQUIRE(urlCancelResult["action"].as<std::string>() == "cancel");
  REQUIRE_FALSE(urlCancelResult.contains("content"));
}

TEST_CASE("Client elicitation/create enforces flat primitive form schema restrictions", "[client][elicitation][validation]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::ElicitationCapability elicitationCapability;
  elicitationCapability.form = true;

  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(std::nullopt, std::nullopt, elicitationCapability, std::nullopt, std::nullopt);
  client->setInitializeConfiguration(std::move(configuration));

  client->setFormElicitationHandler(
    [](const mcp::ElicitationCreateContext &, const mcp::FormElicitationRequest &) -> mcp::FormElicitationResult
    {
      mcp::FormElicitationResult result;
      result.action = mcp::ElicitationAction::kCancel;
      return result;
    });

  auto initializeFuture = client->initialize();
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());

  auto makeBaseFormRequest = [](std::int64_t id) -> mcp::jsonrpc::Request
  {
    mcp::jsonrpc::Request request;
    request.id = id;
    request.method = "elicitation/create";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["mode"] = "form";
    (*request.params)["message"] = "Collect fields";
    (*request.params)["requestedSchema"] = mcp::jsonrpc::JsonValue::object();
    (*request.params)["requestedSchema"]["type"] = "object";
    (*request.params)["requestedSchema"]["properties"] = mcp::jsonrpc::JsonValue::object();
    return request;
  };

  mcp::jsonrpc::Request nestedRequest = makeBaseFormRequest(9230);
  (*nestedRequest.params)["requestedSchema"]["properties"]["profile"] = mcp::jsonrpc::JsonValue::object();
  (*nestedRequest.params)["requestedSchema"]["properties"]["profile"]["type"] = "object";
  const auto nestedResponse = client->handleRequest(mcp::jsonrpc::RequestContext {}, nestedRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(nestedResponse));
  REQUIRE(std::get<mcp::jsonrpc::ErrorResponse>(nestedResponse).error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams));

  mcp::jsonrpc::Request arrayRequest = makeBaseFormRequest(9231);
  (*arrayRequest.params)["requestedSchema"]["properties"]["tags"] = mcp::jsonrpc::JsonValue::object();
  (*arrayRequest.params)["requestedSchema"]["properties"]["tags"]["type"] = "array";
  const auto arrayResponse = client->handleRequest(mcp::jsonrpc::RequestContext {}, arrayRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(arrayResponse));
  REQUIRE(std::get<mcp::jsonrpc::ErrorResponse>(arrayResponse).error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams));

  mcp::jsonrpc::Request primitiveRequest = makeBaseFormRequest(9232);
  (*primitiveRequest.params)["requestedSchema"]["properties"]["name"] = mcp::jsonrpc::JsonValue::object();
  (*primitiveRequest.params)["requestedSchema"]["properties"]["name"]["type"] = "string";
  const auto primitiveResponse = client->handleRequest(mcp::jsonrpc::RequestContext {}, primitiveRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(primitiveResponse));
}

TEST_CASE("Client elicitation/create URL mode rejects malformed absolute URLs", "[client][elicitation][validation]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::ElicitationCapability elicitationCapability;
  elicitationCapability.url = true;

  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(std::nullopt, std::nullopt, elicitationCapability, std::nullopt, std::nullopt);
  client->setInitializeConfiguration(std::move(configuration));

  client->setUrlElicitationHandler(
    [](const mcp::ElicitationCreateContext &, const mcp::UrlElicitationRequest &) -> mcp::UrlElicitationResult
    {
      mcp::UrlElicitationResult result;
      result.action = mcp::ElicitationAction::kCancel;
      return result;
    });

  auto initializeFuture = client->initialize();
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());

  auto makeUrlRequest = [](std::int64_t id, std::string url) -> mcp::jsonrpc::Request
  {
    mcp::jsonrpc::Request request;
    request.id = id;
    request.method = "elicitation/create";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["mode"] = "url";
    (*request.params)["elicitationId"] = "elic-url";
    (*request.params)["message"] = "Open consent page";
    (*request.params)["url"] = std::move(url);
    return request;
  };

  std::vector<std::string> invalidUrls = {
    "ht*tp://example.com/connect",
    "https://",
    "https://example .com/connect",
    "https://example.com/connect\nnext",
    "https://[::1/connect",
    "https://[::1]bad/connect",
    "https://example.com:/connect",
    "https://example.com:70000/connect",
    "https://[::1]:abc/connect",
  };

  std::int64_t requestId = 9250;
  for (const std::string &invalidUrl : invalidUrls)
  {
    const auto response = client->handleRequest(mcp::jsonrpc::RequestContext {}, makeUrlRequest(requestId, invalidUrl)).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams));
    REQUIRE(error.error.message == "elicitation/create url mode requires a valid absolute URL");
    ++requestId;
  }

  const auto validResponse = client->handleRequest(mcp::jsonrpc::RequestContext {}, makeUrlRequest(requestId, "https://[2001:db8::1]:8443/connect")).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(validResponse));
}

TEST_CASE("Client tracks URL elicitation completion notifications and ignores unknown IDs", "[client][elicitation][notifications]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::ElicitationCapability elicitationCapability;
  elicitationCapability.url = true;

  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(std::nullopt, std::nullopt, elicitationCapability, std::nullopt, std::nullopt);
  client->setInitializeConfiguration(std::move(configuration));

  client->setUrlElicitationHandler(
    [](const mcp::ElicitationCreateContext &, const mcp::UrlElicitationRequest &request) -> mcp::UrlElicitationResult
    {
      mcp::UrlElicitationResult result;
      if (request.elicitationId == "known")
      {
        result.action = mcp::ElicitationAction::kAccept;
      }
      else
      {
        result.action = mcp::ElicitationAction::kDecline;
      }

      return result;
    });

  std::vector<std::string> completedIds;
  client->setUrlElicitationCompletionHandler([&completedIds](const mcp::ElicitationCreateContext &, std::string_view elicitationId) -> void
                                             { completedIds.emplace_back(elicitationId); });

  auto initializeFuture = client->initialize();
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());

  mcp::jsonrpc::Request urlRequest;
  urlRequest.id = std::int64_t {9240};
  urlRequest.method = "elicitation/create";
  urlRequest.params = mcp::jsonrpc::JsonValue::object();
  (*urlRequest.params)["mode"] = "url";
  (*urlRequest.params)["elicitationId"] = "known";
  (*urlRequest.params)["message"] = "Open consent page";
  (*urlRequest.params)["url"] = "https://example.com/consent";

  const auto urlResponse = client->handleRequest(mcp::jsonrpc::RequestContext {}, urlRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(urlResponse));
  REQUIRE(completedIds.empty());

  mcp::jsonrpc::Notification unknownNotification;
  unknownNotification.method = "notifications/elicitation/complete";
  unknownNotification.params = mcp::jsonrpc::JsonValue::object();
  (*unknownNotification.params)["elicitationId"] = "unknown";
  client->handleNotification(mcp::jsonrpc::RequestContext {}, unknownNotification);
  REQUIRE(completedIds.empty());

  mcp::jsonrpc::Notification knownNotification;
  knownNotification.method = "notifications/elicitation/complete";
  knownNotification.params = mcp::jsonrpc::JsonValue::object();
  (*knownNotification.params)["elicitationId"] = "known";
  client->handleNotification(mcp::jsonrpc::RequestContext {}, knownNotification);
  REQUIRE(completedIds.size() == 1);
  REQUIRE(completedIds[0] == "known");

  client->handleNotification(mcp::jsonrpc::RequestContext {}, knownNotification);
  REQUIRE(completedIds.size() == 1);

  mcp::jsonrpc::Request declinedRequest = urlRequest;
  declinedRequest.id = std::int64_t {9241};
  (*declinedRequest.params)["elicitationId"] = "declined";
  const auto declinedResponse = client->handleRequest(mcp::jsonrpc::RequestContext {}, declinedRequest).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(declinedResponse));

  mcp::jsonrpc::Notification declinedNotification = knownNotification;
  (*declinedNotification.params)["elicitationId"] = "declined";
  client->handleNotification(mcp::jsonrpc::RequestContext {}, declinedNotification);
  REQUIRE(completedIds.size() == 1);
}

TEST_CASE("Elicitation helper APIs support URL consent and URLElicitationRequiredError", "[client][elicitation][helpers]")
{
  const auto displayInfo = mcp::formatUrlForConsent("  https://example.com:8443/connect?token=redacted  ");
  REQUIRE(displayInfo.has_value());
  if (displayInfo.has_value())
  {
    REQUIRE(displayInfo->fullUrl == "https://example.com:8443/connect?token=redacted");
    REQUIRE(displayInfo->domain == "example.com");
  }

  REQUIRE_FALSE(mcp::formatUrlForConsent("/relative/path").has_value());

  mcp::jsonrpc::JsonValue errorData = mcp::jsonrpc::JsonValue::object();
  errorData["elicitations"] = mcp::jsonrpc::JsonValue::array();
  mcp::jsonrpc::JsonValue elicitation = mcp::jsonrpc::JsonValue::object();
  elicitation["mode"] = "url";
  elicitation["elicitationId"] = "elic-required";
  elicitation["message"] = "Please connect your account";
  elicitation["url"] = "https://example.com/connect";
  errorData["elicitations"].push_back(std::move(elicitation));

  const mcp::JsonRpcError urlRequiredError = mcp::jsonrpc::makeUrlElicitationRequiredError(std::move(errorData), "URL action required");
  REQUIRE(urlRequiredError.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kUrlElicitationRequired));
  REQUIRE(urlRequiredError.message == "URL action required");

  const auto parsed = mcp::parseUrlElicitationRequiredError(urlRequiredError);
  REQUIRE(parsed.has_value());
  if (parsed.has_value())
  {
    REQUIRE(parsed->elicitations.size() == 1);
    REQUIRE(parsed->elicitations[0].elicitationId == "elic-required");
    REQUIRE(parsed->elicitations[0].url == "https://example.com/connect");
  }

  mcp::JsonRpcError wrongCode = urlRequiredError;
  wrongCode.code = static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams);
  REQUIRE_FALSE(mcp::parseUrlElicitationRequiredError(wrongCode).has_value());

  mcp::JsonRpcError missingElicitations = urlRequiredError;
  missingElicitations.data = mcp::jsonrpc::JsonValue::object();
  REQUIRE_FALSE(mcp::parseUrlElicitationRequiredError(missingElicitations).has_value());

  mcp::JsonRpcError malformedItem = urlRequiredError;
  malformedItem.data = mcp::jsonrpc::JsonValue::object();
  (*malformedItem.data)["elicitations"] = mcp::jsonrpc::JsonValue::array();
  mcp::jsonrpc::JsonValue malformedElicitation = mcp::jsonrpc::JsonValue::object();
  malformedElicitation["mode"] = "url";
  malformedElicitation["elicitationId"] = "missing-fields";
  (*malformedItem.data)["elicitations"].push_back(std::move(malformedElicitation));
  REQUIRE_FALSE(mcp::parseUrlElicitationRequiredError(malformedItem).has_value());
}

TEST_CASE("Client pagination helpers detect cursor cycles", "[client][pagination][helpers]")
{
  auto client = mcp::Client::create();

  std::size_t fetchCount = 0;
  auto fetchPage = [&fetchCount](const std::optional<std::string> &cursor) -> mcp::ListToolsResult
  {
    ++fetchCount;

    mcp::ListToolsResult page;
    if (!cursor.has_value())
    {
      page.nextCursor = "repeat-cursor";
      return page;
    }

    page.nextCursor = "repeat-cursor";
    return page;
  };

  try
  {
    client->forEachPage(fetchPage, [](const mcp::ListToolsResult &) -> void {});
    FAIL("Expected cursor cycle detection failure");
  }
  catch (const std::runtime_error &error)
  {
    REQUIRE(std::string(error.what()) == "Pagination cursor cycle detected");
  }
  REQUIRE(fetchCount == 2);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Client pagination helpers enforce max page limit", "[client][pagination][helpers]")
{
  auto client = mcp::Client::create();

  std::size_t fetchCount = 0;
  auto fetchPage = [&fetchCount](const std::optional<std::string> &cursor) -> mcp::ListResourcesResult
  {
    ++fetchCount;

    mcp::ListResourcesResult page;
    page.nextCursor = cursor.has_value() ? *cursor + "-next" : "cursor-1";
    return page;
  };

  try
  {
    client->forEachPage(fetchPage, [](const mcp::ListResourcesResult &) -> void {}, std::nullopt, 2);
    FAIL("Expected max page limit failure in forEachPage");
  }
  catch (const std::runtime_error &error)
  {
    REQUIRE(std::string(error.what()) == "Pagination exceeded maximum page limit");
  }
  REQUIRE(fetchCount == 2);

  try
  {
    static_cast<void>(client->collectAllPages<mcp::ResourceDefinition>(
      fetchPage, [](const mcp::ListResourcesResult &page) -> const std::vector<mcp::ResourceDefinition> & { return page.resources; }, std::nullopt, 1));
    FAIL("Expected max page limit failure in collectAllPages");
  }
  catch (const std::runtime_error &error)
  {
    REQUIRE(std::string(error.what()) == "Pagination exceeded maximum page limit");
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Client pagination helpers pass and honor cursors for list endpoints", "[client][pagination][helpers]")
{
  mcp::ToolsCapability toolsCapability;
  mcp::ResourcesCapability resourcesCapability;
  mcp::PromptsCapability promptsCapability;

  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, resourcesCapability, toolsCapability, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));

  for (std::size_t index = 0; index < kRoundTripItemCount; ++index)
  {
    server->registerTool(makeToolDefinition("cursor-tool-" + std::to_string(index)),
                         [](const mcp::ToolCallContext &) -> mcp::CallToolResult
                         {
                           mcp::CallToolResult result;
                           result.content = mcp::jsonrpc::JsonValue::array();
                           return result;
                         });

    const std::string uri = "resource://cursor-item-" + std::to_string(index);
    mcp::ResourceDefinition resourceDefinition;
    resourceDefinition.uri = uri;
    resourceDefinition.name = "cursor-resource-" + std::to_string(index);
    // NOLINTNEXTLINE(bugprone-exception-escape)
    server->registerResource(resourceDefinition,
                             [uri](const mcp::ResourceReadContext &) -> std::vector<mcp::ResourceContent>  // NOLINT(bugprone-exception-escape)
                             {
                               return {
                                 mcp::ResourceContent::text(uri, "value-" + uri, std::string("text/plain")),
                               };
                             });

    mcp::ResourceTemplateDefinition templateDefinition;
    templateDefinition.uriTemplate = "resource://cursor-template/{id-" + std::to_string(index) + "}";
    templateDefinition.name = "cursor-template-" + std::to_string(index);
    server->registerResourceTemplate(std::move(templateDefinition));

    server->registerPrompt(makePromptDefinition("cursor-prompt-" + std::to_string(index)),
                           [](const mcp::PromptGetContext &) -> mcp::PromptGetResult
                           {
                             mcp::PromptGetResult result;
                             result.messages = {};
                             return result;
                           });
  }

  auto client = mcp::Client::create();
  auto transport = std::make_shared<InMemoryClientServerTransport>(server, client);
  client->attachTransport(transport);
  client->start();

  const auto initializeResponse = client->initialize().get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initializeResponse));

  std::vector<std::optional<std::string>> toolsRequestCursors;
  std::vector<std::optional<std::string>> toolsResponseCursors;
  std::size_t observedTools = 0;
  client->forEachPage(
    [&client, &toolsRequestCursors, &toolsResponseCursors](const std::optional<std::string> &cursor) -> mcp::ListToolsResult
    {
      toolsRequestCursors.push_back(cursor);
      mcp::ListToolsResult page = client->listTools(cursor);
      toolsResponseCursors.push_back(page.nextCursor);
      return page;
    },
    [&observedTools](const mcp::ListToolsResult &page) -> void { observedTools += page.tools.size(); });

  REQUIRE(toolsRequestCursors.size() == 2);
  REQUIRE_FALSE(toolsRequestCursors[0].has_value());
  REQUIRE(toolsResponseCursors[0].has_value());
  REQUIRE(toolsRequestCursors[1] == toolsResponseCursors[0]);
  REQUIRE_FALSE(toolsResponseCursors[1].has_value());
  REQUIRE(observedTools == kRoundTripItemCount);

  std::vector<std::optional<std::string>> resourcesRequestCursors;
  std::vector<std::optional<std::string>> resourcesResponseCursors;
  std::size_t observedResources = 0;
  client->forEachPage(
    [&client, &resourcesRequestCursors, &resourcesResponseCursors](const std::optional<std::string> &cursor) -> mcp::ListResourcesResult
    {
      resourcesRequestCursors.push_back(cursor);
      mcp::ListResourcesResult page = client->listResources(cursor);
      resourcesResponseCursors.push_back(page.nextCursor);
      return page;
    },
    [&observedResources](const mcp::ListResourcesResult &page) -> void { observedResources += page.resources.size(); });

  REQUIRE(resourcesRequestCursors.size() == 2);
  REQUIRE_FALSE(resourcesRequestCursors[0].has_value());
  REQUIRE(resourcesResponseCursors[0].has_value());
  REQUIRE(resourcesRequestCursors[1] == resourcesResponseCursors[0]);
  REQUIRE_FALSE(resourcesResponseCursors[1].has_value());
  REQUIRE(observedResources == kRoundTripItemCount);

  std::vector<std::optional<std::string>> promptsRequestCursors;
  std::vector<std::optional<std::string>> promptsResponseCursors;
  std::size_t observedPrompts = 0;
  client->forEachPage(
    [&client, &promptsRequestCursors, &promptsResponseCursors](const std::optional<std::string> &cursor) -> mcp::ListPromptsResult
    {
      promptsRequestCursors.push_back(cursor);
      mcp::ListPromptsResult page = client->listPrompts(cursor);
      promptsResponseCursors.push_back(page.nextCursor);
      return page;
    },
    [&observedPrompts](const mcp::ListPromptsResult &page) -> void { observedPrompts += page.prompts.size(); });

  REQUIRE(promptsRequestCursors.size() == 2);
  REQUIRE_FALSE(promptsRequestCursors[0].has_value());
  REQUIRE(promptsResponseCursors[0].has_value());
  REQUIRE(promptsRequestCursors[1] == promptsResponseCursors[0]);
  REQUIRE_FALSE(promptsResponseCursors[1].has_value());
  REQUIRE(observedPrompts == kRoundTripItemCount);

  std::vector<std::optional<std::string>> templatesRequestCursors;
  std::vector<std::optional<std::string>> templatesResponseCursors;
  std::size_t observedTemplates = 0;
  client->forEachPage(
    [&client, &templatesRequestCursors, &templatesResponseCursors](const std::optional<std::string> &cursor) -> mcp::ListResourceTemplatesResult
    {
      templatesRequestCursors.push_back(cursor);
      mcp::ListResourceTemplatesResult page = client->listResourceTemplates(cursor);
      templatesResponseCursors.push_back(page.nextCursor);
      return page;
    },
    [&observedTemplates](const mcp::ListResourceTemplatesResult &page) -> void { observedTemplates += page.resourceTemplates.size(); });

  REQUIRE(templatesRequestCursors.size() == 2);
  REQUIRE_FALSE(templatesRequestCursors[0].has_value());
  REQUIRE(templatesResponseCursors[0].has_value());
  REQUIRE(templatesRequestCursors[1] == templatesResponseCursors[0]);
  REQUIRE_FALSE(templatesResponseCursors[1].has_value());
  REQUIRE(observedTemplates == kRoundTripItemCount);

  const auto firstToolsPage = client->listTools();
  REQUIRE(firstToolsPage.nextCursor.has_value());

  std::vector<std::optional<std::string>> resumedToolsRequestCursors;
  const auto resumedTools = client->collectAllPages<mcp::ToolDefinition>(
    [&client, &resumedToolsRequestCursors](const std::optional<std::string> &cursor) -> mcp::ListToolsResult
    {
      resumedToolsRequestCursors.push_back(cursor);
      return client->listTools(cursor);
    },
    [](const mcp::ListToolsResult &page) -> const std::vector<mcp::ToolDefinition> & { return page.tools; },
    firstToolsPage.nextCursor);

  REQUIRE(resumedToolsRequestCursors.size() == 1);
  REQUIRE(resumedToolsRequestCursors[0] == firstToolsPage.nextCursor);
  REQUIRE(resumedTools.size() == (kRoundTripItemCount - firstToolsPage.tools.size()));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Client convenience APIs support local in-memory round-trips and pagination helpers", "[client][features][roundtrip]")
{
  mcp::ToolsCapability toolsCapability;
  mcp::ResourcesCapability resourcesCapability;
  mcp::PromptsCapability promptsCapability;

  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, resourcesCapability, toolsCapability, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));

  for (std::size_t index = 0; index < kRoundTripItemCount; ++index)
  {
    const std::string toolName = "tool-" + std::to_string(index);
    server->registerTool(makeToolDefinition(toolName),
                         [](const mcp::ToolCallContext &context) -> mcp::CallToolResult
                         {
                           mcp::CallToolResult result;
                           result.content = mcp::jsonrpc::JsonValue::array();

                           mcp::jsonrpc::JsonValue text = mcp::jsonrpc::JsonValue::object();
                           text["type"] = "text";
                           text["text"] = context.arguments["value"].as<std::string>();
                           result.content.push_back(std::move(text));
                           return result;
                         });

    const std::string uri = "resource://item-" + std::to_string(index);
    mcp::ResourceDefinition resourceDefinition;
    resourceDefinition.uri = uri;
    resourceDefinition.name = "resource-" + std::to_string(index);
    // NOLINTNEXTLINE(bugprone-exception-escape)
    server->registerResource(resourceDefinition,
                             [uri](const mcp::ResourceReadContext &) -> std::vector<mcp::ResourceContent>  // NOLINT(bugprone-exception-escape)
                             {
                               return {
                                 mcp::ResourceContent::text(uri, "value-" + uri, std::string("text/plain")),
                               };
                             });

    mcp::ResourceTemplateDefinition templateDefinition;
    templateDefinition.uriTemplate = "resource://template/{id-" + std::to_string(index) + "}";
    templateDefinition.name = "template-" + std::to_string(index);
    server->registerResourceTemplate(std::move(templateDefinition));

    server->registerPrompt(makePromptDefinition("prompt-" + std::to_string(index)),
                           [](const mcp::PromptGetContext &context) -> mcp::PromptGetResult
                           {
                             mcp::PromptGetResult result;
                             result.description = "generated prompt";

                             mcp::PromptMessage message;
                             message.role = "user";
                             message.content = mcp::jsonrpc::JsonValue::object();
                             message.content["type"] = "text";
                             message.content["text"] = "Explain: " + context.arguments["topic"].as<std::string>();
                             result.messages.push_back(std::move(message));
                             return result;
                           });
  }

  auto client = mcp::Client::create();
  auto transport = std::make_shared<InMemoryClientServerTransport>(server, client);
  client->attachTransport(transport);
  client->start();

  const auto initializeResponse = client->initialize().get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initializeResponse));

  const auto firstToolsPage = client->listTools();
  REQUIRE(firstToolsPage.tools.size() == 50);
  REQUIRE(firstToolsPage.nextCursor.has_value());

  const auto allTools =
    client->collectAllPages<mcp::ToolDefinition>([&client](const std::optional<std::string> &cursor) -> mcp::ListToolsResult { return client->listTools(cursor); },
                                                 [](const mcp::ListToolsResult &page) -> const std::vector<mcp::ToolDefinition> & { return page.tools; });
  REQUIRE(allTools.size() == kRoundTripItemCount);

  std::size_t resourcePages = 0;
  std::size_t totalResources = 0;
  client->forEachPage([&client](const std::optional<std::string> &cursor) -> mcp::ListResourcesResult { return client->listResources(cursor); },
                      [&resourcePages, &totalResources](const mcp::ListResourcesResult &page) -> void
                      {
                        ++resourcePages;
                        totalResources += page.resources.size();
                      });
  REQUIRE(resourcePages == 2);
  REQUIRE(totalResources == kRoundTripItemCount);

  const auto readResult = client->readResource("resource://item-0");
  REQUIRE(readResult.contents.size() == 1);
  REQUIRE(readResult.contents[0].uri == "resource://item-0");
  REQUIRE(readResult.contents[0].kind == mcp::ResourceContentKind::kText);
  REQUIRE(readResult.contents[0].value == "value-resource://item-0");

  const auto allTemplates = client->collectAllPages<mcp::ResourceTemplateDefinition>(
    [&client](const std::optional<std::string> &cursor) -> mcp::ListResourceTemplatesResult { return client->listResourceTemplates(cursor); },
    [](const mcp::ListResourceTemplatesResult &page) -> const std::vector<mcp::ResourceTemplateDefinition> & { return page.resourceTemplates; });
  REQUIRE(allTemplates.size() == kRoundTripItemCount);

  const auto allPrompts =
    client->collectAllPages<mcp::PromptDefinition>([&client](const std::optional<std::string> &cursor) -> mcp::ListPromptsResult { return client->listPrompts(cursor); },
                                                   [](const mcp::ListPromptsResult &page) -> const std::vector<mcp::PromptDefinition> & { return page.prompts; });
  REQUIRE(allPrompts.size() == kRoundTripItemCount);

  mcp::jsonrpc::JsonValue toolArgs = mcp::jsonrpc::JsonValue::object();
  toolArgs["value"] = "hello-tool";
  const auto toolResult = client->callTool("tool-0", std::move(toolArgs));
  REQUIRE(toolResult.content.size() == 1);
  REQUIRE(toolResult.content[0]["type"].as<std::string>() == "text");
  REQUIRE(toolResult.content[0]["text"].as<std::string>() == "hello-tool");

  mcp::jsonrpc::JsonValue promptArgs = mcp::jsonrpc::JsonValue::object();
  promptArgs["topic"] = "pagination";
  const auto promptResult = client->getPrompt("prompt-0", std::move(promptArgs));
  REQUIRE(promptResult.description.has_value());
  REQUIRE(promptResult.description.value_or("") == "generated prompt");
  REQUIRE(promptResult.messages.size() == 1);
  REQUIRE(promptResult.messages[0].role == "user");
  REQUIRE(promptResult.messages[0].content["text"].as<std::string>() == "Explain: pagination");
}

TEST_CASE("Client connectHttp using HttpClientOptions performs lifecycle requests", "[client][transport][http]")
{
  mcp::transport::http::StreamableHttpServer streamableServer;
  streamableServer.setRequestHandler(
    [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> mcp::transport::http::StreamableRequestResult
    {
      mcp::transport::http::StreamableRequestResult result;

      mcp::jsonrpc::SuccessResponse response;
      response.id = request.id;
      response.result = mcp::jsonrpc::JsonValue::object();
      if (request.method == "initialize")
      {
        response.result["protocolVersion"] = std::string(mcp::kLatestProtocolVersion);
        response.result["capabilities"] = mcp::jsonrpc::JsonValue::object();
        response.result["serverInfo"] = mcp::jsonrpc::JsonValue::object();
        response.result["serverInfo"]["name"] = "http-test-server";
        response.result["serverInfo"]["version"] = "1.0.0";
      }

      result.response = std::move(response);
      return result;
    });

  mcp::transport::http::HttpServerOptions serverOptions;
  serverOptions.endpoint.path = "/mcp";
  serverOptions.endpoint.bindAddress = "127.0.0.1";
  serverOptions.endpoint.bindLocalhostOnly = true;
  serverOptions.endpoint.port = 0;

  mcp::transport::http::HttpServerRuntime serverRuntime(serverOptions);
  serverRuntime.setRequestHandler([&streamableServer](const mcp::transport::http::ServerRequest &request) -> mcp::transport::http::ServerResponse
                                  { return streamableServer.handleRequest(request); });
  serverRuntime.start();

  auto client = mcp::Client::create();
  mcp::transport::http::HttpClientOptions clientOptions;
  clientOptions.endpointUrl = "http://127.0.0.1:" + std::to_string(serverRuntime.localPort()) + "/mcp";

  client->connectHttp(clientOptions);
  client->start();

  const auto initializeResponse = client->initialize().get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initializeResponse));

  const auto pingResponse = client->sendRequest("ping", mcp::jsonrpc::JsonValue::object()).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(pingResponse));

  client->stop();
  serverRuntime.stop();
}

#ifdef MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH
TEST_CASE("Client connectStdio spawns subprocess and performs initialize/ping", "[client][transport][stdio]")
{
  auto client = mcp::Client::create();

  mcp::transport::StdioClientOptions options;
  options.executablePath = MCP_TEST_STDIO_SUBPROCESS_HELPER_PATH;
  client->connectStdio(options);
  client->start();

  const auto initializeResponse = client->initialize().get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initializeResponse));

  const auto pingResponse = client->sendRequest("ping", mcp::jsonrpc::JsonValue::object()).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(pingResponse));

  client->stop();
}
#endif

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Client server-initiated request handling does not deadlock during stop", "[client][threading][deadlock]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::RootsCapability rootsCapability;
  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(rootsCapability, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  client->setInitializeConfiguration(std::move(configuration));

  client->setRootsProvider(
    [](const mcp::RootsListContext &) -> std::vector<mcp::RootEntry>
    {
      return {
        mcp::RootEntry {"file:///workspace/project-a", std::optional<std::string> {"Project A"}, std::nullopt},
      };
    });

  auto initializeFuture = client->initialize();
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());

  std::promise<void> handlerEnteredPromise;
  auto handlerEnteredFuture = handlerEnteredPromise.get_future();
  std::promise<void> releaseHandlerPromise;
  auto releaseHandlerFuture = releaseHandlerPromise.get_future();
  std::promise<void> handlerFinishedPromise;
  auto handlerFinishedFuture = handlerFinishedPromise.get_future();

  std::atomic<bool> stopCompleted = false;
  std::atomic<bool> stopRaisedException = false;

  client->setRootsProvider(
    [&handlerEnteredPromise, &releaseHandlerFuture, &handlerFinishedPromise](const mcp::RootsListContext &) -> std::vector<mcp::RootEntry>
    {
      handlerEnteredPromise.set_value();
      releaseHandlerFuture.wait();

      std::vector<mcp::RootEntry> roots;
      roots.push_back(mcp::RootEntry {"file:///workspace/project-b", std::optional<std::string> {"Project B"}, std::nullopt});
      handlerFinishedPromise.set_value();
      return roots;
    });

  mcp::jsonrpc::Request listRootsRequest;
  listRootsRequest.id = std::int64_t {9500};
  listRootsRequest.method = "roots/list";
  listRootsRequest.params = mcp::jsonrpc::JsonValue::object();

  auto requestFuture = std::async(
    std::launch::async, [&client, &listRootsRequest]() -> mcp::jsonrpc::Response { return client->handleRequest(mcp::jsonrpc::RequestContext {}, listRootsRequest).get(); });

  REQUIRE(handlerEnteredFuture.wait_for(std::chrono::seconds(1)) == std::future_status::ready);

  auto stopFuture = std::async(std::launch::async,
                               [&client, &stopCompleted, &stopRaisedException]() -> void
                               {
                                 try
                                 {
                                   client->stop();
                                   stopCompleted.store(true);
                                 }
                                 catch (const std::exception &)
                                 {
                                   stopRaisedException.store(true);
                                 }
                               });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  releaseHandlerPromise.set_value();

  REQUIRE(handlerFinishedFuture.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
  REQUIRE(requestFuture.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
  REQUIRE(stopFuture.wait_for(std::chrono::seconds(1)) == std::future_status::ready);

  const auto response = requestFuture.get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response));

  REQUIRE(stopCompleted.load());
  REQUIRE_FALSE(stopRaisedException.load());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Client provider exceptions result in deterministic JSON-RPC error responses", "[client][exceptions][error_handling]")
{
  auto client = mcp::Client::create();
  auto transport = std::make_shared<RecordingTransport>();
  client->attachTransport(transport);
  client->start();

  mcp::RootsCapability rootsCapability;
  mcp::SamplingCapability samplingCapability;
  mcp::ElicitationCapability elicitationCapability;
  elicitationCapability.form = true;

  mcp::ClientInitializeConfiguration configuration;
  configuration.capabilities = mcp::ClientCapabilities(rootsCapability, samplingCapability, elicitationCapability, std::nullopt, std::nullopt);
  client->setInitializeConfiguration(std::move(configuration));

  auto initializeFuture = client->initialize();
  const auto outboundMessages = transport->messages();
  REQUIRE(outboundMessages.size() == 1);
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());
  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));
  static_cast<void>(initializeFuture.get());

  SECTION("roots/list provider exception returns internal error")
  {
    client->setRootsProvider([](const mcp::RootsListContext &) -> std::vector<mcp::RootEntry> { throw std::runtime_error("Simulated roots provider failure"); });

    mcp::jsonrpc::Request listRootsRequest;
    listRootsRequest.id = std::int64_t {9600};
    listRootsRequest.method = "roots/list";
    listRootsRequest.params = mcp::jsonrpc::JsonValue::object();

    const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, listRootsRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInternalError));
    REQUIRE(error.error.message.find("roots/list failed") != std::string::npos);
    REQUIRE(error.error.message.find("Simulated roots provider failure") != std::string::npos);
  }

  SECTION("sampling/createMessage handler exception returns internal error")
  {
    client->setSamplingCreateMessageHandler([](const mcp::SamplingCreateMessageContext &, const mcp::jsonrpc::JsonValue &) -> std::optional<mcp::jsonrpc::JsonValue>
                                            { throw std::runtime_error("Simulated sampling handler failure"); });

    mcp::jsonrpc::Request samplingRequest;
    samplingRequest.id = std::int64_t {9601};
    samplingRequest.method = "sampling/createMessage";
    samplingRequest.params = mcp::jsonrpc::JsonValue::object();
    (*samplingRequest.params)["maxTokens"] = 64;
    (*samplingRequest.params)["messages"] = mcp::jsonrpc::JsonValue::array();
    mcp::jsonrpc::JsonValue message = mcp::jsonrpc::JsonValue::object();
    message["role"] = "user";
    message["content"] = mcp::jsonrpc::JsonValue::object();
    message["content"]["type"] = "text";
    message["content"]["text"] = "hello";
    (*samplingRequest.params)["messages"].push_back(std::move(message));

    const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, samplingRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInternalError));
    REQUIRE(error.error.message.find("sampling/createMessage failed") != std::string::npos);
    REQUIRE(error.error.message.find("Simulated sampling handler failure") != std::string::npos);
  }

  SECTION("elicitation/create form handler exception returns internal error")
  {
    client->setFormElicitationHandler([](const mcp::ElicitationCreateContext &, const mcp::FormElicitationRequest &) -> mcp::FormElicitationResult
                                      { throw std::runtime_error("Simulated form elicitation failure"); });

    mcp::jsonrpc::Request elicitationRequest;
    elicitationRequest.id = std::int64_t {9602};
    elicitationRequest.method = "elicitation/create";
    elicitationRequest.params = mcp::jsonrpc::JsonValue::object();
    (*elicitationRequest.params)["mode"] = "form";
    (*elicitationRequest.params)["message"] = "Collect project name";
    (*elicitationRequest.params)["requestedSchema"] = mcp::jsonrpc::JsonValue::object();
    (*elicitationRequest.params)["requestedSchema"]["type"] = "object";
    (*elicitationRequest.params)["requestedSchema"]["properties"] = mcp::jsonrpc::JsonValue::object();
    (*elicitationRequest.params)["requestedSchema"]["properties"]["name"] = mcp::jsonrpc::JsonValue::object();
    (*elicitationRequest.params)["requestedSchema"]["properties"]["name"]["type"] = "string";

    const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, elicitationRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInternalError));
    REQUIRE(error.error.message.find("elicitation/create form handling failed") != std::string::npos);
    REQUIRE(error.error.message.find("Simulated form elicitation failure") != std::string::npos);
  }

  SECTION("elicitation/create url handler exception returns internal error")
  {
    // Need to re-initialize client with url capability enabled
    client = mcp::Client::create();
    transport = std::make_shared<RecordingTransport>();
    client->attachTransport(transport);
    client->start();

    mcp::ElicitationCapability urlElicitationCapability;
    urlElicitationCapability.url = true;

    mcp::ClientInitializeConfiguration urlConfiguration;
    urlConfiguration.capabilities = mcp::ClientCapabilities(std::nullopt, std::nullopt, urlElicitationCapability, std::nullopt, std::nullopt);
    client->setInitializeConfiguration(std::move(urlConfiguration));

    auto urlInitFuture = client->initialize();
    const auto urlOutboundMessages = transport->messages();
    REQUIRE(urlOutboundMessages.size() == 1);
    const auto &urlInitRequest = std::get<mcp::jsonrpc::Request>(urlOutboundMessages.front());
    REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(urlInitRequest.id)));
    static_cast<void>(urlInitFuture.get());

    client->setUrlElicitationHandler([](const mcp::ElicitationCreateContext &, const mcp::UrlElicitationRequest &) -> mcp::UrlElicitationResult
                                     { throw std::runtime_error("Simulated URL elicitation failure"); });

    mcp::jsonrpc::Request elicitationRequest;
    elicitationRequest.id = std::int64_t {9603};
    elicitationRequest.method = "elicitation/create";
    elicitationRequest.params = mcp::jsonrpc::JsonValue::object();
    (*elicitationRequest.params)["mode"] = "url";
    (*elicitationRequest.params)["elicitationId"] = "elic-test-1";
    (*elicitationRequest.params)["message"] = "Open settings page";
    (*elicitationRequest.params)["url"] = "https://example.com/settings";

    const mcp::jsonrpc::Response response = client->handleRequest(mcp::jsonrpc::RequestContext {}, elicitationRequest).get();
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response);
    REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInternalError));
    REQUIRE(error.error.message.find("elicitation/create url handling failed") != std::string::npos);
    REQUIRE(error.error.message.find("Simulated URL elicitation failure") != std::string::npos);
  }
}
