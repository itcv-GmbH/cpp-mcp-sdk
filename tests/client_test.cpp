#include <chrono>
#include <condition_variable>
#include <cstddef>
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
#include <mcp/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/prompts.hpp>
#include <mcp/server/resources.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/tools.hpp>
#include <mcp/transport/transport.hpp>
#include <mcp/version.hpp>

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
  mcp::SessionOptions options;
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

  mcp::transport::HttpServerOptions serverOptions;
  serverOptions.endpoint.path = "/mcp";
  serverOptions.endpoint.bindAddress = "127.0.0.1";
  serverOptions.endpoint.bindLocalhostOnly = true;
  serverOptions.endpoint.port = 0;

  mcp::transport::HttpServerRuntime serverRuntime(serverOptions);
  serverRuntime.setRequestHandler([&streamableServer](const mcp::transport::http::ServerRequest &request) -> mcp::transport::http::ServerResponse
                                  { return streamableServer.handleRequest(request); });
  serverRuntime.start();

  auto client = mcp::Client::create();
  mcp::transport::HttpClientOptions clientOptions;
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
