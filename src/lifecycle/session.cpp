#include <algorithm>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <jsoncons/basic_json.hpp>
#include <mcp/detail/initialize_codec.hpp>
#include <mcp/sdk/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/transport/transport.hpp>
#include <mcp/sdk/version.hpp>

namespace mcp
{

static constexpr std::string_view kInitializeMethod = "initialize";
static constexpr std::string_view kPingMethod = "ping";
static constexpr std::string_view kLoggingSetLevelMethod = "logging/setLevel";
static constexpr std::string_view kNotificationInitialized = "notifications/initialized";
static constexpr std::string_view kNotificationMessage = "notifications/message";
static constexpr std::string_view kDefaultClientName = "mcp-cpp-client";
static constexpr std::string_view kDefaultServerName = "mcp-cpp-sdk";

static auto isVersionSupported(const std::vector<std::string> &supportedVersions, std::string_view version) -> bool
{
  return std::any_of(supportedVersions.begin(), supportedVersions.end(), [version](const std::string &supportedVersion) -> bool { return supportedVersion == version; });
}

static auto selectServerProtocolVersion(std::string_view clientProposed, const std::vector<std::string> &serverSupported) -> std::optional<std::string>
{
  if (isVersionSupported(serverSupported, clientProposed))
  {
    return std::string(clientProposed);
  }

  if (serverSupported.empty())
  {
    return std::nullopt;
  }

  return *std::max_element(serverSupported.begin(), serverSupported.end());
}

static auto jsonArrayFromStrings(const std::vector<std::string> &values) -> jsoncons::json
{
  return jsoncons::json::array(values.begin(), values.end());
}

static auto negotiationFailureData(std::string_view requested, const std::vector<std::string> &supported) -> jsoncons::json
{
  jsoncons::json data = jsoncons::json::object();
  data["requested"] = std::string(requested);
  data["supported"] = jsonArrayFromStrings(supported);
  return data;
}

static auto joinedVersions(const std::vector<std::string> &versions) -> std::string
{
  std::ostringstream stream;
  for (std::size_t index = 0; index < versions.size(); ++index)
  {
    if (index > 0)
    {
      stream << ", ";
    }

    stream << versions[index];
  }

  return stream.str();
}

static auto formatVersionList(const std::vector<std::string> &versions) -> std::string
{
  return "[" + joinedVersions(versions) + "]";
}

static auto negotiationFailureMessage(std::string_view requested, const std::vector<std::string> &supported) -> std::string
{
  return "Protocol negotiation failed: requested '" + std::string(requested) + "', supported: " + formatVersionList(supported);
}

static auto ensureClientInitializeDefaults(jsonrpc::JsonValue &params, const std::vector<std::string> &supportedVersions) -> void
{
  if (!params.is_object())
  {
    params = jsoncons::json::object();
  }

  if (!params.contains("protocolVersion") || !params["protocolVersion"].is_string())
  {
    if (!supportedVersions.empty())
    {
      params["protocolVersion"] = supportedVersions.front();
    }
    else
    {
      params["protocolVersion"] = std::string(kLatestProtocolVersion);
    }
  }

  if (!params.contains("capabilities") || !params["capabilities"].is_object())
  {
    params["capabilities"] = jsoncons::json::object();
  }

  if (!params.contains("clientInfo") || !params["clientInfo"].is_object())
  {
    params["clientInfo"] = jsoncons::json::object();
  }

  if (!params["clientInfo"].contains("name") || !params["clientInfo"]["name"].is_string())
  {
    params["clientInfo"]["name"] = std::string(kDefaultClientName);
  }

  if (!params["clientInfo"].contains("version") || !params["clientInfo"]["version"].is_string())
  {
    params["clientInfo"]["version"] = std::string(kSdkVersion);
  }
}

// Icon implementation
Icon::Icon(std::string src, std::optional<std::string> mimeType, std::optional<std::vector<std::string>> sizes, std::optional<std::string> theme)
  : src_(std::move(src))
  , mimeType_(std::move(mimeType))
  , sizes_(std::move(sizes))
  , theme_(std::move(theme))
{
}

// Implementation implementation
Implementation::Implementation(std::string name,
                               std::string version,
                               std::optional<std::string> title,
                               std::optional<std::string> description,
                               std::optional<std::string> websiteUrl,
                               std::optional<std::vector<Icon>> icons)
  : name_(std::move(name))
  , version_(std::move(version))
  , title_(std::move(title))
  , description_(std::move(description))
  , websiteUrl_(std::move(websiteUrl))
  , icons_(std::move(icons))
{
}

// ClientCapabilities implementation
ClientCapabilities::ClientCapabilities(std::optional<RootsCapability> roots,
                                       std::optional<SamplingCapability> sampling,
                                       std::optional<ElicitationCapability> elicitation,
                                       std::optional<TasksCapability> tasks,
                                       std::optional<jsoncons::json> experimental)
  : roots_(roots)
  , sampling_(sampling)
  , elicitation_(elicitation)
  , tasks_(tasks)
  , experimental_(std::move(experimental))
{
}

auto ClientCapabilities::roots() const noexcept -> const std::optional<RootsCapability> &
{
  return roots_;
}

auto ClientCapabilities::sampling() const noexcept -> const std::optional<SamplingCapability> &
{
  return sampling_;
}

auto ClientCapabilities::elicitation() const noexcept -> const std::optional<ElicitationCapability> &
{
  return elicitation_;
}

auto ClientCapabilities::tasks() const noexcept -> const std::optional<TasksCapability> &
{
  return tasks_;
}

auto ClientCapabilities::experimental() const noexcept -> const std::optional<jsoncons::json> &
{
  return experimental_;
}

auto ClientCapabilities::hasCapability(std::string_view capability) const -> bool
{
  return (capability == "roots" && roots_.has_value()) || (capability == "sampling" && sampling_.has_value()) || (capability == "elicitation" && elicitation_.has_value())
    || (capability == "tasks" && tasks_.has_value()) || (capability == "experimental" && experimental_.has_value());
}

// ServerCapabilities implementation
ServerCapabilities::ServerCapabilities(std::optional<LoggingCapability> logging,
                                       std::optional<CompletionsCapability> completions,
                                       std::optional<PromptsCapability> prompts,
                                       std::optional<ResourcesCapability> resources,
                                       std::optional<ToolsCapability> tools,
                                       std::optional<TasksCapability> tasks,
                                       std::optional<jsoncons::json> experimental)
  : logging_(logging)
  , completions_(completions)
  , prompts_(prompts)
  , resources_(resources)
  , tools_(tools)
  , tasks_(tasks)
  , experimental_(std::move(experimental))
{
}

auto ServerCapabilities::logging() const noexcept -> const std::optional<LoggingCapability> &
{
  return logging_;
}

auto ServerCapabilities::completions() const noexcept -> const std::optional<CompletionsCapability> &
{
  return completions_;
}

auto ServerCapabilities::prompts() const noexcept -> const std::optional<PromptsCapability> &
{
  return prompts_;
}

auto ServerCapabilities::resources() const noexcept -> const std::optional<ResourcesCapability> &
{
  return resources_;
}

auto ServerCapabilities::tools() const noexcept -> const std::optional<ToolsCapability> &
{
  return tools_;
}

auto ServerCapabilities::tasks() const noexcept -> const std::optional<TasksCapability> &
{
  return tasks_;
}

auto ServerCapabilities::experimental() const noexcept -> const std::optional<jsoncons::json> &
{
  return experimental_;
}

auto ServerCapabilities::hasCapability(std::string_view capability) const -> bool
{
  return (capability == "logging" && logging_.has_value()) || (capability == "completions" && completions_.has_value()) || (capability == "prompts" && prompts_.has_value())
    || (capability == "resources" && resources_.has_value()) || (capability == "tools" && tools_.has_value()) || (capability == "tasks" && tasks_.has_value())
    || (capability == "experimental" && experimental_.has_value());
}

// NegotiatedParameters implementation
NegotiatedParameters::NegotiatedParameters(std::string protocolVersion,
                                           ClientCapabilities clientCaps,
                                           ServerCapabilities serverCaps,
                                           Implementation clientInfo,
                                           Implementation serverInfo,
                                           std::optional<std::string> instructions)
  : protocolVersion_(std::move(protocolVersion))
  , clientCapabilities_(std::move(clientCaps))
  , serverCapabilities_(std::move(serverCaps))
  , clientInfo_(std::move(clientInfo))
  , serverInfo_(std::move(serverInfo))
  , instructions_(std::move(instructions))
{
}

auto NegotiatedParameters::protocolVersion() const noexcept -> std::string_view
{
  return protocolVersion_;
}

auto NegotiatedParameters::clientCapabilities() const noexcept -> const ClientCapabilities &
{
  return clientCapabilities_;
}

auto NegotiatedParameters::serverCapabilities() const noexcept -> const ServerCapabilities &
{
  return serverCapabilities_;
}

auto NegotiatedParameters::clientInfo() const noexcept -> const Implementation &
{
  return clientInfo_;
}

auto NegotiatedParameters::serverInfo() const noexcept -> const Implementation &
{
  return serverInfo_;
}

auto NegotiatedParameters::instructions() const noexcept -> const std::optional<std::string> &
{
  return instructions_;
}

// Session implementation
Session::Session(SessionOptions options)
  : options_(std::move(options))
  , configuredServerInfo_(std::string(kDefaultServerName), std::string(kSdkVersion))
  , router_(jsonrpc::RouterOptions {.errorReporter = options_.errorReporter})
{
}

auto Session::registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void
{
  const std::scoped_lock lock(mutex_);
  router_.registerRequestHandler(std::move(method), std::move(handler));
}

auto Session::registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void
{
  const std::scoped_lock lock(mutex_);
  router_.registerNotificationHandler(std::move(method), std::move(handler));
}

auto Session::enforceOutboundRequestLifecycle(std::string_view method, jsonrpc::JsonValue params, RequestOptions options) -> void
{
  static_cast<void>(options);

  {
    const std::scoped_lock lock(mutex_);

    if (role_ == SessionRole::kClient)
    {
      if (state_ == SessionState::kCreated && method != kInitializeMethod)
      {
        throw LifecycleError("Client must send 'initialize' as the first request");
      }

      if (state_ == SessionState::kInitializing && method != kPingMethod)
      {
        throw LifecycleError("Client can only send 'ping' requests while waiting for initialize response");
      }

      if (state_ == SessionState::kInitialized)
      {
        throw LifecycleError("Client must send 'notifications/initialized' before normal requests");
      }

      if (method == kInitializeMethod)
      {
        if (state_ != SessionState::kCreated)
        {
          throw LifecycleError("Client can only send 'initialize' from Created state");
        }

        ensureClientInitializeDefaults(params, options_.supportedProtocolVersions);
        pendingClientCapabilities_ = detail::parseClientCapabilities(params["capabilities"]);
        pendingClientInfo_ = detail::parseImplementation(params["clientInfo"], std::string(kDefaultClientName), std::string(kSdkVersion));
        state_ = SessionState::kInitializing;
      }
    }

    if (role_ == SessionRole::kServer && state_ != SessionState::kOperating && method != kPingMethod)
    {
      throw LifecycleError("Server cannot send feature requests before receiving 'notifications/initialized'");
    }
  }
}

auto Session::sendRequest(const std::string &method, jsonrpc::JsonValue params, RequestOptions options) -> std::future<jsonrpc::Response>
{
  enforceOutboundRequestLifecycle(method, std::move(params), options);

  // Compatibility wrapper: Session no longer performs transport I/O for requests.
  std::promise<jsonrpc::Response> promise;
  promise.set_value(jsonrpc::ErrorResponse {});  // Placeholder
  return promise.get_future();
}

auto Session::sendRequestAsync(const std::string &method, jsonrpc::JsonValue params, const ResponseCallback &callback, RequestOptions options) -> void
{
  std::future<jsonrpc::Response> responseFuture = sendRequest(method, std::move(params), options);
  callback(responseFuture.get());
}

auto Session::sendNotification(std::string method, std::optional<jsonrpc::JsonValue> params) -> void
{
  std::shared_ptr<transport::Transport> transport;

  {
    const std::scoped_lock lock(mutex_);

    if (role_ == SessionRole::kClient)
    {
      if (state_ == SessionState::kInitialized && method != kNotificationInitialized)
      {
        throw LifecycleError("Client must send 'notifications/initialized' before other notifications");
      }

      if (method == kNotificationInitialized)
      {
        if (state_ != SessionState::kInitialized)
        {
          throw LifecycleError("Client cannot send 'notifications/initialized' before initialize completes");
        }

        state_ = SessionState::kOperating;
      }
    }

    if (role_ == SessionRole::kServer && state_ != SessionState::kOperating)
    {
      const bool loggingNotificationAllowed = state_ == SessionState::kInitialized && method == kNotificationMessage;
      if (!loggingNotificationAllowed)
      {
        throw LifecycleError("Server cannot send notifications before initialization completes");
      }
    }

    transport = transport_;
  }

  if (transport)
  {
    jsonrpc::Notification notification;
    notification.method = std::move(method);
    notification.params = std::move(params);
    transport->send(jsonrpc::Message {std::move(notification)});
  }
}

auto Session::attachTransport(std::shared_ptr<transport::Transport> transport) -> void
{
  const std::scoped_lock lock(mutex_);
  transport_ = std::move(transport);
}

auto Session::start() -> void
{
  const std::scoped_lock lock(mutex_);
  if (state_ != SessionState::kCreated)
  {
    throw LifecycleError("Session can only be started from Created state");
  }
  // State remains kCreated until initialize is sent (client) or received (server)
}

auto Session::stop() noexcept -> void
{
  const std::scoped_lock lock(mutex_);
  if (state_ == SessionState::kStopped)
  {
    return;
  }
  state_ = SessionState::kStopping;
  // Transport and request-dispatch shutdown are owned by Client/Server and Router.
  state_ = SessionState::kStopped;
}

auto Session::state() const noexcept -> SessionState
{
  const std::scoped_lock lock(mutex_);
  return state_;
}

auto Session::negotiatedProtocolVersion() const noexcept -> std::optional<std::string_view>
{
  const std::scoped_lock lock(mutex_);
  if (!negotiatedParams_)
  {
    return std::nullopt;
  }
  return negotiatedParams_->protocolVersion();
}

auto Session::supportedProtocolVersions() const -> const std::vector<std::string> &
{
  return options_.supportedProtocolVersions;
}

auto Session::negotiatedParameters() const -> const std::optional<NegotiatedParameters> &
{
  const std::scoped_lock lock(mutex_);
  return negotiatedParams_;
}

auto Session::setRole(SessionRole role) -> void
{
  const std::scoped_lock lock(mutex_);
  role_ = role;
}

auto Session::role() const noexcept -> SessionRole
{
  const std::scoped_lock lock(mutex_);
  return role_;
}

auto Session::handleInitializeRequest(const jsonrpc::Request &request) -> jsonrpc::Response
{
  const std::scoped_lock lock(mutex_);

  if (role_ != SessionRole::kServer)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidRequestError(std::nullopt, "Only a server session can handle initialize requests"), request.id);
  }

  if (request.method != kInitializeMethod)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidRequestError(std::nullopt, "Expected 'initialize' request"), request.id);
  }

  // Server enforcement: initialize must be the first request
  if (state_ != SessionState::kCreated)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidRequestError(std::nullopt, "Initialize must be the first request"), request.id);
  }

  // Parse initialize request params
  if (!request.params || !request.params->is_object())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "Missing initialize params object"), request.id);
  }

  const auto &params = *request.params;

  // Extract protocol version from client
  if (!params.contains("protocolVersion") || !params["protocolVersion"].is_string())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "Missing or invalid protocolVersion"), request.id);
  }

  const std::string clientProposedVersion = params["protocolVersion"].as<std::string>();

  if (!params.contains("capabilities") || !params["capabilities"].is_object())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "Missing or invalid capabilities object"), request.id);
  }

  if (!params.contains("clientInfo") || !params["clientInfo"].is_object())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "Missing or invalid clientInfo object"), request.id);
  }

  const ClientCapabilities clientCaps = detail::parseClientCapabilities(params["capabilities"]);
  const Implementation clientInfo = detail::parseImplementation(params["clientInfo"], std::string(kDefaultClientName), std::string(kSdkVersion));

  const auto negotiatedVersion = selectServerProtocolVersion(clientProposedVersion, options_.supportedProtocolVersions);

  if (!negotiatedVersion)
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(negotiationFailureData(clientProposedVersion, options_.supportedProtocolVersions),
                                                                      negotiationFailureMessage(clientProposedVersion, options_.supportedProtocolVersions)),
                                      request.id);
  }

  // Store negotiated parameters
  negotiatedParams_.emplace(*negotiatedVersion, clientCaps, configuredServerCapabilities_, clientInfo, configuredServerInfo_, configuredServerInstructions_);

  // Transition to initialized state
  state_ = SessionState::kInitialized;

  // Build initialize result
  jsoncons::json result = jsoncons::json::object();
  result["protocolVersion"] = *negotiatedVersion;
  result["capabilities"] = detail::serverCapabilitiesToJson(configuredServerCapabilities_);
  result["serverInfo"] = detail::implementationToJson(configuredServerInfo_);
  if (configuredServerInstructions_.has_value())
  {
    result["instructions"] = *configuredServerInstructions_;
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = std::move(result);
  return response;
}

auto Session::handleInitializeResponse(const jsonrpc::Response &response) -> void
{
  std::unique_lock<std::mutex> lock(mutex_);

  if (role_ != SessionRole::kClient)
  {
    throw LifecycleError("Only a client session can handle initialize responses");
  }

  if (state_ != SessionState::kInitializing)
  {
    throw LifecycleError("Received initialize response while session is not initializing");
  }

  if (std::holds_alternative<jsonrpc::ErrorResponse>(response))
  {
    const auto &errorResponse = std::get<jsonrpc::ErrorResponse>(response);
    state_ = SessionState::kCreated;

    std::string message = "Initialize failed";
    if (!errorResponse.error.message.empty())
    {
      message += ": " + errorResponse.error.message;
    }

    throw LifecycleError(message);
  }

  const auto &successResp = std::get<jsonrpc::SuccessResponse>(response);

  // Parse negotiated version from response
  if (!successResp.result.contains("protocolVersion") || !successResp.result["protocolVersion"].is_string())
  {
    state_ = SessionState::kCreated;
    throw LifecycleError("Initialize response missing required 'protocolVersion'");
  }

  const std::string serverVersion = successResp.result["protocolVersion"].as<std::string>();

  // Check if we support the server's chosen version
  if (!isVersionSupported(options_.supportedProtocolVersions, serverVersion))
  {
    state_ = SessionState::kCreated;
    throw LifecycleError("Server selected unsupported protocol version '" + serverVersion + "'. Client supports: [" + joinedVersions(options_.supportedProtocolVersions) + "]");
  }

  if (!successResp.result.contains("capabilities") || !successResp.result["capabilities"].is_object())
  {
    state_ = SessionState::kCreated;
    throw LifecycleError("Initialize response missing required 'capabilities' object");
  }

  if (!successResp.result.contains("serverInfo") || !successResp.result["serverInfo"].is_object())
  {
    state_ = SessionState::kCreated;
    throw LifecycleError("Initialize response missing required 'serverInfo' object");
  }

  const ServerCapabilities serverCaps = detail::parseServerCapabilities(successResp.result["capabilities"]);
  const Implementation serverInfo = detail::parseImplementation(successResp.result["serverInfo"], std::string(kDefaultServerName), std::string(kSdkVersion));

  // Parse instructions if present
  std::optional<std::string> instructions;
  if (successResp.result.contains("instructions") && successResp.result["instructions"].is_string())
  {
    instructions = successResp.result["instructions"].as<std::string>();
  }

  ClientCapabilities clientCaps;
  if (pendingClientCapabilities_.has_value())
  {
    clientCaps = *pendingClientCapabilities_;
  }

  Implementation clientInfo {std::string(kDefaultClientName), std::string(kSdkVersion)};
  if (pendingClientInfo_.has_value())
  {
    clientInfo = *pendingClientInfo_;
  }

  negotiatedParams_.emplace(serverVersion, std::move(clientCaps), serverCaps, std::move(clientInfo), serverInfo, std::move(instructions));
  pendingClientCapabilities_.reset();
  pendingClientInfo_.reset();

  state_ = SessionState::kInitialized;

  lock.unlock();

  sendNotification(std::string(kNotificationInitialized));
}

auto Session::handleInitializedNotification() -> void
{
  const std::scoped_lock lock(mutex_);

  // Server enforcement: only transition to operating after receiving initialized
  if (state_ == SessionState::kInitialized)
  {
    state_ = SessionState::kOperating;
  }
}

auto Session::configureServerInitialization(ServerCapabilities capabilities, Implementation serverInfo, std::optional<std::string> instructions) -> void
{
  const std::scoped_lock lock(mutex_);

  if (state_ != SessionState::kCreated)
  {
    throw LifecycleError("Server initialization configuration must be set before the session starts initialization");
  }

  configuredServerCapabilities_ = std::move(capabilities);
  configuredServerInfo_ = std::move(serverInfo);
  configuredServerInstructions_ = std::move(instructions);
}

auto Session::canHandleRequest(std::string_view method) const -> bool
{
  const std::scoped_lock lock(mutex_);

  // Server enforcement: pre-initialization request handling
  if (role_ == SessionRole::kServer && state_ != SessionState::kOperating)
  {
    // Only allow initialize, ping, and logging/setLevel before initialized
    if (method == kInitializeMethod)
    {
      return state_ == SessionState::kCreated;
    }
    if (method == kPingMethod)
    {
      return true;  // Always allowed
    }
    if (method == kLoggingSetLevelMethod)
    {
      // Only allowed if server is in initialized state (after initialize response but before notifications/initialized)
      return state_ == SessionState::kInitialized;
    }
    return false;
  }

  return true;
}

auto Session::canSendRequest(std::string_view method) const -> bool
{
  const std::scoped_lock lock(mutex_);

  // Client enforcement: pre-initialization request sending
  if (role_ == SessionRole::kClient)
  {
    if (state_ == SessionState::kCreated)
    {
      // Only initialize allowed
      return method == kInitializeMethod;
    }
    if (state_ == SessionState::kInitializing)
    {
      // Only ping allowed
      return method == kPingMethod;
    }
    if (state_ == SessionState::kInitialized)
    {
      // Only initialized notification allowed
      return false;  // Use sendNotification for this
    }
  }

  // Server enforcement: pre-initialized request sending
  if (role_ == SessionRole::kServer && state_ != SessionState::kOperating)
  {
    return method == kPingMethod;
  }

  return true;
}

auto Session::canSendNotification(std::string_view method) const -> bool
{
  const std::scoped_lock lock(mutex_);

  if (role_ == SessionRole::kClient)
  {
    if (method == kNotificationInitialized)
    {
      return state_ == SessionState::kInitialized;
    }

    if (state_ == SessionState::kInitialized)
    {
      return false;
    }
  }

  // Server enforcement: logging notifications
  if (role_ == SessionRole::kServer && method == kNotificationMessage)
  {
    return state_ == SessionState::kInitialized || state_ == SessionState::kOperating;
  }

  return role_ != SessionRole::kServer || state_ == SessionState::kOperating;
}

auto Session::checkCapability(std::string_view capability) const -> bool
{
  const std::scoped_lock lock(mutex_);

  if (!negotiatedParams_)
  {
    return false;
  }

  // Check client capabilities if we're the server
  if (role_ == SessionRole::kServer)
  {
    return negotiatedParams_->clientCapabilities().hasCapability(capability);
  }

  // Check server capabilities if we're the client
  if (role_ == SessionRole::kClient)
  {
    return negotiatedParams_->serverCapabilities().hasCapability(capability);
  }

  return false;
}

}  // namespace mcp
