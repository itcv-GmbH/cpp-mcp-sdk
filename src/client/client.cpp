#include <algorithm>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <mcp/client/client.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/transport/stdio.hpp>
#include <mcp/transport/transport.hpp>
#include <mcp/version.hpp>

namespace mcp
{
static constexpr std::string_view kInitializeMethod = "initialize";
static constexpr std::string_view kPingMethod = "ping";
static constexpr std::string_view kInitializedNotificationMethod = "notifications/initialized";
static constexpr std::string_view kDefaultClientName = "mcp-cpp-client";

static auto makeReadyResponseFuture(jsonrpc::Response response) -> std::future<jsonrpc::Response>
{
  std::promise<jsonrpc::Response> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

static auto latestSupportedVersion(const std::vector<std::string> &supportedVersions) -> std::string
{
  if (supportedVersions.empty())
  {
    return std::string(kLatestProtocolVersion);
  }

  return *std::max_element(supportedVersions.begin(), supportedVersions.end());
}

static auto extractResponseId(const jsonrpc::Response &response) -> std::optional<jsonrpc::RequestId>
{
  if (std::holds_alternative<jsonrpc::SuccessResponse>(response))
  {
    return std::get<jsonrpc::SuccessResponse>(response).id;
  }

  return std::get<jsonrpc::ErrorResponse>(response).id;
}

static auto iconToJson(const Icon &icon) -> jsonrpc::JsonValue
{
  jsonrpc::JsonValue iconJson = jsonrpc::JsonValue::object();
  iconJson["src"] = icon.src();

  if (icon.mimeType().has_value())
  {
    iconJson["mimeType"] = *icon.mimeType();
  }

  if (icon.sizes().has_value())
  {
    iconJson["sizes"] = jsonrpc::JsonValue::array(icon.sizes()->begin(), icon.sizes()->end());
  }

  if (icon.theme().has_value())
  {
    iconJson["theme"] = *icon.theme();
  }

  return iconJson;
}

static auto implementationToJson(const Implementation &implementation) -> jsonrpc::JsonValue
{
  jsonrpc::JsonValue implementationJson = jsonrpc::JsonValue::object();
  implementationJson["name"] = implementation.name();
  implementationJson["version"] = implementation.version();

  if (implementation.title().has_value())
  {
    implementationJson["title"] = *implementation.title();
  }

  if (implementation.description().has_value())
  {
    implementationJson["description"] = *implementation.description();
  }

  if (implementation.websiteUrl().has_value())
  {
    implementationJson["websiteUrl"] = *implementation.websiteUrl();
  }

  if (implementation.icons().has_value())
  {
    jsonrpc::JsonValue iconsJson = jsonrpc::JsonValue::array();
    for (const auto &icon : *implementation.icons())
    {
      iconsJson.push_back(iconToJson(icon));
    }

    implementationJson["icons"] = std::move(iconsJson);
  }

  return implementationJson;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static auto clientCapabilitiesToJson(const ClientCapabilities &capabilities) -> jsonrpc::JsonValue
{
  jsonrpc::JsonValue capabilitiesJson = jsonrpc::JsonValue::object();

  if (capabilities.experimental().has_value())
  {
    capabilitiesJson["experimental"] = *capabilities.experimental();
  }

  if (capabilities.roots().has_value())
  {
    jsonrpc::JsonValue rootsJson = jsonrpc::JsonValue::object();
    rootsJson["listChanged"] = capabilities.roots()->listChanged;
    capabilitiesJson["roots"] = std::move(rootsJson);
  }

  if (capabilities.sampling().has_value())
  {
    jsonrpc::JsonValue samplingJson = jsonrpc::JsonValue::object();
    if (capabilities.sampling()->context)
    {
      samplingJson["context"] = jsonrpc::JsonValue::object();
    }

    if (capabilities.sampling()->tools)
    {
      samplingJson["tools"] = jsonrpc::JsonValue::object();
    }

    capabilitiesJson["sampling"] = std::move(samplingJson);
  }

  if (capabilities.elicitation().has_value())
  {
    jsonrpc::JsonValue elicitationJson = jsonrpc::JsonValue::object();
    if (capabilities.elicitation()->form)
    {
      elicitationJson["form"] = jsonrpc::JsonValue::object();
    }

    if (capabilities.elicitation()->url)
    {
      elicitationJson["url"] = jsonrpc::JsonValue::object();
    }

    capabilitiesJson["elicitation"] = std::move(elicitationJson);
  }

  if (capabilities.tasks().has_value())
  {
    jsonrpc::JsonValue tasksJson = jsonrpc::JsonValue::object();
    if (capabilities.tasks()->list)
    {
      tasksJson["list"] = jsonrpc::JsonValue::object();
    }

    if (capabilities.tasks()->cancel)
    {
      tasksJson["cancel"] = jsonrpc::JsonValue::object();
    }

    if (capabilities.tasks()->samplingCreateMessage || capabilities.tasks()->elicitationCreate || capabilities.tasks()->toolsCall)
    {
      jsonrpc::JsonValue requestsJson = jsonrpc::JsonValue::object();
      if (capabilities.tasks()->samplingCreateMessage)
      {
        jsonrpc::JsonValue samplingJson = jsonrpc::JsonValue::object();
        samplingJson["createMessage"] = jsonrpc::JsonValue::object();
        requestsJson["sampling"] = std::move(samplingJson);
      }

      if (capabilities.tasks()->elicitationCreate)
      {
        jsonrpc::JsonValue elicitationJson = jsonrpc::JsonValue::object();
        elicitationJson["create"] = jsonrpc::JsonValue::object();
        requestsJson["elicitation"] = std::move(elicitationJson);
      }

      if (capabilities.tasks()->toolsCall)
      {
        jsonrpc::JsonValue toolsJson = jsonrpc::JsonValue::object();
        toolsJson["call"] = jsonrpc::JsonValue::object();
        requestsJson["tools"] = std::move(toolsJson);
      }

      tasksJson["requests"] = std::move(requestsJson);
    }

    capabilitiesJson["tasks"] = std::move(tasksJson);
  }

  return capabilitiesJson;
}

class StreamableHttpClientTransport final : public transport::Transport
{
public:
  StreamableHttpClientTransport(transport::http::StreamableHttpClientOptions options,
                                transport::http::StreamableHttpClient::RequestExecutor requestExecutor,
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
  transport::http::StreamableHttpClient client_;
  std::function<void(const jsonrpc::Message &)> inboundMessageHandler_;
};

auto Client::create(SessionOptions options) -> std::shared_ptr<Client>
{
  return std::make_shared<Client>(std::make_shared<Session>(std::move(options)));
}

Client::Client(std::shared_ptr<Session> session)
  : session_(std::move(session))
{
  if (!session_)
  {
    throw std::invalid_argument("Client requires a non-null session");
  }

  session_->setRole(SessionRole::kClient);

  router_.setOutboundMessageSender([this](const jsonrpc::RequestContext &, jsonrpc::Message message) -> void { dispatchOutboundMessage(std::move(message)); });

  router_.registerRequestHandler(std::string(kPingMethod),
                                 [](const jsonrpc::RequestContext &, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 {
                                   jsonrpc::SuccessResponse response;
                                   response.id = request.id;
                                   response.result = jsonrpc::JsonValue::object();
                                   return makeReadyResponseFuture(jsonrpc::Response {std::move(response)});
                                 });
}

auto Client::session() const noexcept -> const std::shared_ptr<Session> &
{
  return session_;
}

auto Client::attachTransport(std::shared_ptr<transport::Transport> transport) -> void
{
  if (!transport)
  {
    throw std::invalid_argument("Client transport must not be null");
  }

  {
    const std::scoped_lock lock(mutex_);
    transport_ = std::move(transport);
  }

  session_->attachTransport(transport_);
  transport_->attach(session_);
}

auto Client::connectStdio(transport::StdioClientOptions options) -> void
{
  attachTransport(std::make_shared<transport::StdioTransport>(std::move(options)));
}

auto Client::connectHttp(const transport::HttpClientOptions &options) -> void
{
  attachTransport(std::make_shared<transport::HttpTransport>(options));
}

auto Client::connectHttp(transport::http::StreamableHttpClientOptions options, transport::http::StreamableHttpClient::RequestExecutor requestExecutor) -> void
{
  auto transport = std::make_shared<StreamableHttpClientTransport>(
    std::move(options), std::move(requestExecutor), [this](const jsonrpc::Message &inboundMessage) -> void { handleMessage(jsonrpc::RequestContext {}, inboundMessage); });

  attachTransport(std::move(transport));
}

auto Client::setInitializeConfiguration(ClientInitializeConfiguration configuration) -> void
{
  const std::scoped_lock lock(mutex_);
  initializeConfiguration_ = std::move(configuration);
}

auto Client::initializeConfiguration() const -> ClientInitializeConfiguration
{
  const std::scoped_lock lock(mutex_);
  return initializeConfiguration_;
}

auto Client::initialize(RequestOptions options) -> std::future<jsonrpc::Response>
{
  return sendRequest(std::string(kInitializeMethod), jsonrpc::JsonValue::object(), options);
}

auto Client::start() -> void
{
  session_->setRole(SessionRole::kClient);
  session_->start();

  std::shared_ptr<transport::Transport> transport;
  {
    const std::scoped_lock lock(mutex_);
    transport = transport_;
  }

  if (transport && !transport->isRunning())
  {
    transport->start();
  }
}

auto Client::stop() -> void
{
  std::shared_ptr<transport::Transport> transport;
  {
    const std::scoped_lock lock(mutex_);
    transport = transport_;
  }

  if (transport && transport->isRunning())
  {
    transport->stop();
  }

  session_->stop();
}

auto Client::sendRequest(std::string method, jsonrpc::JsonValue params, RequestOptions options) -> std::future<jsonrpc::Response>
{
  jsonrpc::Request request;
  request.id = nextRequestId();
  request.method = std::move(method);
  request.params = std::move(params);

  if (request.method == kInitializeMethod)
  {
    applyInitializeDefaults(request);
  }

  const jsonrpc::JsonValue lifecycleParams = request.params.has_value() ? *request.params : jsonrpc::JsonValue::object();
  static_cast<void>(session_->sendRequest(request.method, lifecycleParams, options));

  {
    const std::scoped_lock lock(mutex_);
    if (request.method == kInitializeMethod)
    {
      pendingInitializeRequestId_ = request.id;
    }
  }

  jsonrpc::OutboundRequestOptions outboundOptions;
  outboundOptions.timeout = options.timeout;
  outboundOptions.cancelOnTimeout = options.cancelOnTimeout;
  return router_.sendRequest(jsonrpc::RequestContext {}, std::move(request), std::move(outboundOptions));
}

auto Client::sendRequestAsync(std::string method, jsonrpc::JsonValue params, const ResponseCallback &callback, RequestOptions options) -> void
{
  auto responseFuture = sendRequest(std::move(method), std::move(params), options);
  callback(responseFuture.get());
}

auto Client::sendNotification(std::string method, std::optional<jsonrpc::JsonValue> params) -> void
{
  session_->sendNotification(std::move(method), std::move(params));
}

auto Client::registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void
{
  router_.registerRequestHandler(std::move(method), std::move(handler));
}

auto Client::registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void
{
  router_.registerNotificationHandler(std::move(method), std::move(handler));
}

auto Client::handleRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
{
  return router_.dispatchRequest(context, request);
}

auto Client::handleNotification(const jsonrpc::RequestContext &context, const jsonrpc::Notification &notification) -> void
{
  if (notification.method == kInitializedNotificationMethod)
  {
    session_->handleInitializedNotification();
  }

  router_.dispatchNotification(context, notification);
}

auto Client::handleResponse(const jsonrpc::RequestContext &context, const jsonrpc::Response &response) -> bool
{
  const bool initializeResponse = isPendingInitializeResponse(response);
  if (initializeResponse)
  {
    const std::scoped_lock lock(mutex_);
    pendingInitializeRequestId_.reset();
  }

  const bool dispatched = router_.dispatchResponse(context, response);

  if (initializeResponse)
  {
    session_->handleInitializeResponse(response);
  }

  return dispatched;
}

auto Client::handleMessage(const jsonrpc::RequestContext &context, const jsonrpc::Message &message) -> void
{
  if (std::holds_alternative<jsonrpc::Request>(message))
  {
    const auto &request = std::get<jsonrpc::Request>(message);
    const jsonrpc::Response response = handleRequest(context, request).get();
    std::visit([this](const auto &typedResponse) -> void { dispatchOutboundMessage(jsonrpc::Message {typedResponse}); }, response);
    return;
  }

  if (std::holds_alternative<jsonrpc::Notification>(message))
  {
    handleNotification(context, std::get<jsonrpc::Notification>(message));
    return;
  }

  if (std::holds_alternative<jsonrpc::SuccessResponse>(message))
  {
    static_cast<void>(handleResponse(context, jsonrpc::Response {std::get<jsonrpc::SuccessResponse>(message)}));
    return;
  }

  static_cast<void>(handleResponse(context, jsonrpc::Response {std::get<jsonrpc::ErrorResponse>(message)}));
}

auto Client::negotiatedProtocolVersion() const noexcept -> std::optional<std::string_view>
{
  return session_->negotiatedProtocolVersion();
}

auto Client::negotiatedParameters() const -> std::optional<NegotiatedParameters>
{
  const auto &negotiated = session_->negotiatedParameters();
  if (!negotiated.has_value())
  {
    return std::nullopt;
  }

  return negotiated;
}

auto Client::negotiatedClientCapabilities() const -> std::optional<ClientCapabilities>
{
  const auto negotiated = negotiatedParameters();
  if (!negotiated.has_value())
  {
    return std::nullopt;
  }

  return negotiated->clientCapabilities();
}

auto Client::negotiatedServerCapabilities() const -> std::optional<ServerCapabilities>
{
  const auto negotiated = negotiatedParameters();
  if (!negotiated.has_value())
  {
    return std::nullopt;
  }

  return negotiated->serverCapabilities();
}

auto Client::supportedProtocolVersions() const -> std::vector<std::string>
{
  return session_->supportedProtocolVersions();
}

auto Client::nextRequestId() -> jsonrpc::RequestId
{
  const std::scoped_lock lock(mutex_);
  return jsonrpc::RequestId {nextRequestId_++};
}

auto Client::applyInitializeDefaults(jsonrpc::Request &request) const -> void
{
  jsonrpc::JsonValue params = request.params.has_value() ? *request.params : jsonrpc::JsonValue::object();
  if (!params.is_object())
  {
    params = jsonrpc::JsonValue::object();
  }

  ClientInitializeConfiguration initializeConfiguration;
  {
    const std::scoped_lock lock(mutex_);
    initializeConfiguration = initializeConfiguration_;
  }

  if (!params.contains("protocolVersion") || !params["protocolVersion"].is_string())
  {
    if (initializeConfiguration.protocolVersion.has_value())
    {
      params["protocolVersion"] = *initializeConfiguration.protocolVersion;
    }
    else
    {
      params["protocolVersion"] = latestSupportedVersion(session_->supportedProtocolVersions());
    }
  }

  if (!params.contains("capabilities") || !params["capabilities"].is_object())
  {
    if (initializeConfiguration.capabilities.has_value())
    {
      params["capabilities"] = clientCapabilitiesToJson(*initializeConfiguration.capabilities);
    }
    else
    {
      params["capabilities"] = jsonrpc::JsonValue::object();
    }
  }

  if (!params.contains("clientInfo") || !params["clientInfo"].is_object())
  {
    if (initializeConfiguration.clientInfo.has_value())
    {
      params["clientInfo"] = implementationToJson(*initializeConfiguration.clientInfo);
    }
    else
    {
      const Implementation clientInfo {std::string(kDefaultClientName), std::string(kSdkVersion)};
      params["clientInfo"] = implementationToJson(clientInfo);
    }
  }

  request.params = std::move(params);
}

auto Client::dispatchOutboundMessage(jsonrpc::Message message) -> void
{
  std::shared_ptr<transport::Transport> transport;
  {
    const std::scoped_lock lock(mutex_);
    transport = transport_;
  }

  if (!transport)
  {
    throw std::runtime_error("Client transport is not attached");
  }

  if (!transport->isRunning())
  {
    throw std::runtime_error("Client transport must be running before sending messages");
  }

  transport->send(std::move(message));
}

auto Client::isPendingInitializeResponse(const jsonrpc::Response &response) const -> bool
{
  const auto responseId = extractResponseId(response);
  if (!responseId.has_value())
  {
    return false;
  }

  const std::scoped_lock lock(mutex_);
  return pendingInitializeRequestId_.has_value() && *pendingInitializeRequestId_ == *responseId;
}

}  // namespace mcp
