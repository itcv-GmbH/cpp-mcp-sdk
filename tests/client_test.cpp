#include <atomic>
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
  }

  [[nodiscard]] auto messages() const -> std::vector<mcp::jsonrpc::Message>
  {
    const std::scoped_lock lock(mutex_);
    return messages_;
  }

private:
  mutable std::mutex mutex_;
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

static auto makeFailedInitializeResponse(const mcp::jsonrpc::RequestId &requestId) -> mcp::jsonrpc::Response
{
  mcp::jsonrpc::ErrorResponse response;
  response.id = requestId;
  response.error.code = static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidParams);
  response.error.message = "Initialize rejected";
  return mcp::jsonrpc::Response {std::move(response)};
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

  std::atomic<bool> sendRequestAsyncReturned = false;
  std::promise<std::thread::id> callerThreadIdPromise;
  auto callerThreadIdFuture = callerThreadIdPromise.get_future();

  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  bool callbackInvoked = false;
  std::thread::id callbackThreadId;
  std::optional<mcp::jsonrpc::Response> callbackResponse;

  std::thread caller(
    [&]() -> void
    {
      callerThreadIdPromise.set_value(std::this_thread::get_id());
      client->sendRequestAsync("initialize",
                               mcp::jsonrpc::JsonValue::object(),
                               [&](const mcp::jsonrpc::Response &response) -> void
                               {
                                 {
                                   const std::scoped_lock lock(callbackMutex);
                                   callbackInvoked = true;
                                   callbackThreadId = std::this_thread::get_id();
                                   callbackResponse = response;
                                 }
                                 callbackCv.notify_one();
                               });
      sendRequestAsyncReturned.store(true, std::memory_order_release);
    });

  const auto callerThreadId = callerThreadIdFuture.get();

  const auto waitDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds {500};
  while (transport->messages().empty() && std::chrono::steady_clock::now() < waitDeadline)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds {1});
  }

  REQUIRE(!transport->messages().empty());
  REQUIRE(sendRequestAsyncReturned.load(std::memory_order_acquire));

  {
    const std::scoped_lock lock(callbackMutex);
    REQUIRE(!callbackInvoked);
  }

  const auto outboundMessages = transport->messages();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(outboundMessages.front()));
  const auto &initializeRequest = std::get<mcp::jsonrpc::Request>(outboundMessages.front());

  REQUIRE(client->handleResponse(mcp::jsonrpc::RequestContext {}, makeSuccessfulInitializeResponse(initializeRequest.id)));

  {
    std::unique_lock<std::mutex> lock(callbackMutex);
    REQUIRE(callbackCv.wait_for(lock, std::chrono::milliseconds {500}, [&]() -> bool { return callbackInvoked; }));
  }

  REQUIRE(callbackResponse.has_value());
  const auto callbackResult = callbackResponse.value_or(mcp::jsonrpc::Response {mcp::jsonrpc::ErrorResponse {}});
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(callbackResult));
  REQUIRE(callbackThreadId != callerThreadId);

  caller.join();
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
