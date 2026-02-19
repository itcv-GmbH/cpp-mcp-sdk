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
#include <mcp/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/transport/transport.hpp>
#include <mcp/version.hpp>

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

static auto parseIcon(const jsoncons::json &iconJson) -> std::optional<Icon>
{
  if (!iconJson.is_object() || !iconJson.contains("src") || !iconJson["src"].is_string())
  {
    return std::nullopt;
  }

  std::optional<std::string> mimeType;
  if (iconJson.contains("mimeType") && iconJson["mimeType"].is_string())
  {
    mimeType = iconJson["mimeType"].as<std::string>();
  }

  std::optional<std::vector<std::string>> sizes;
  if (iconJson.contains("sizes") && iconJson["sizes"].is_array())
  {
    std::vector<std::string> parsedSizes;
    for (const auto &sizeValue : iconJson["sizes"].array_range())
    {
      if (sizeValue.is_string())
      {
        parsedSizes.push_back(sizeValue.as<std::string>());
      }
    }

    sizes = std::move(parsedSizes);
  }

  std::optional<std::string> theme;
  if (iconJson.contains("theme") && iconJson["theme"].is_string())
  {
    theme = iconJson["theme"].as<std::string>();
  }

  return Icon(iconJson["src"].as<std::string>(), std::move(mimeType), std::move(sizes), std::move(theme));
}

static auto parseImplementation(const jsoncons::json &implementationJson, std::string defaultName, std::string defaultVersion) -> Implementation
{
  if (!implementationJson.is_object())
  {
    return {std::move(defaultName), std::move(defaultVersion)};
  }

  std::string name = implementationJson.contains("name") && implementationJson["name"].is_string() ? implementationJson["name"].as<std::string>() : std::move(defaultName);

  std::string version =
    implementationJson.contains("version") && implementationJson["version"].is_string() ? implementationJson["version"].as<std::string>() : std::move(defaultVersion);

  std::optional<std::string> title;
  if (implementationJson.contains("title") && implementationJson["title"].is_string())
  {
    title = implementationJson["title"].as<std::string>();
  }

  std::optional<std::string> description;
  if (implementationJson.contains("description") && implementationJson["description"].is_string())
  {
    description = implementationJson["description"].as<std::string>();
  }

  std::optional<std::string> websiteUrl;
  if (implementationJson.contains("websiteUrl") && implementationJson["websiteUrl"].is_string())
  {
    websiteUrl = implementationJson["websiteUrl"].as<std::string>();
  }

  std::optional<std::vector<Icon>> icons;
  if (implementationJson.contains("icons") && implementationJson["icons"].is_array())
  {
    std::vector<Icon> parsedIcons;
    for (const auto &iconValue : implementationJson["icons"].array_range())
    {
      const auto parsedIcon = parseIcon(iconValue);
      if (parsedIcon.has_value())
      {
        parsedIcons.push_back(*parsedIcon);
      }
    }

    icons = std::move(parsedIcons);
  }

  return {std::move(name), std::move(version), std::move(title), std::move(description), std::move(websiteUrl), std::move(icons)};
}

static auto iconToJson(const Icon &icon) -> jsoncons::json
{
  jsoncons::json iconJson = jsoncons::json::object();
  iconJson["src"] = icon.src();

  if (icon.mimeType().has_value())
  {
    iconJson["mimeType"] = *icon.mimeType();
  }

  if (icon.sizes().has_value())
  {
    iconJson["sizes"] = jsoncons::json::array(icon.sizes()->begin(), icon.sizes()->end());
  }

  if (icon.theme().has_value())
  {
    iconJson["theme"] = *icon.theme();
  }

  return iconJson;
}

static auto implementationToJson(const Implementation &implementation) -> jsoncons::json
{
  jsoncons::json implementationJson = jsoncons::json::object();
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
    jsoncons::json iconsJson = jsoncons::json::array();
    for (const auto &icon : *implementation.icons())
    {
      iconsJson.push_back(iconToJson(icon));
    }

    implementationJson["icons"] = std::move(iconsJson);
  }

  return implementationJson;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static auto parseClientCapabilities(const jsoncons::json &capabilitiesJson) -> ClientCapabilities
{
  if (!capabilitiesJson.is_object())
  {
    return ClientCapabilities {};
  }

  std::optional<RootsCapability> roots;
  if (capabilitiesJson.contains("roots") && capabilitiesJson["roots"].is_object())
  {
    RootsCapability rootsCapability;
    if (capabilitiesJson["roots"].contains("listChanged") && capabilitiesJson["roots"]["listChanged"].is_bool())
    {
      rootsCapability.listChanged = capabilitiesJson["roots"]["listChanged"].as<bool>();
    }

    roots = rootsCapability;
  }

  std::optional<SamplingCapability> sampling;
  if (capabilitiesJson.contains("sampling") && capabilitiesJson["sampling"].is_object())
  {
    SamplingCapability samplingCapability;
    samplingCapability.context = capabilitiesJson["sampling"].contains("context") && capabilitiesJson["sampling"]["context"].is_object();
    samplingCapability.tools = capabilitiesJson["sampling"].contains("tools") && capabilitiesJson["sampling"]["tools"].is_object();
    sampling = samplingCapability;
  }

  std::optional<ElicitationCapability> elicitation;
  if (capabilitiesJson.contains("elicitation") && capabilitiesJson["elicitation"].is_object())
  {
    const auto &elicitationJson = capabilitiesJson["elicitation"];
    ElicitationCapability elicitationCapability;
    elicitationCapability.form = elicitationJson.contains("form") && elicitationJson["form"].is_object();
    elicitationCapability.url = elicitationJson.contains("url") && elicitationJson["url"].is_object();
    if (!elicitationCapability.form && !elicitationCapability.url && elicitationJson.empty())
    {
      elicitationCapability.form = true;
    }

    elicitation = elicitationCapability;
  }

  std::optional<TasksCapability> tasks;
  if (capabilitiesJson.contains("tasks") && capabilitiesJson["tasks"].is_object())
  {
    TasksCapability tasksCapability;
    const auto &tasksJson = capabilitiesJson["tasks"];
    tasksCapability.list = tasksJson.contains("list") && tasksJson["list"].is_object();
    tasksCapability.cancel = tasksJson.contains("cancel") && tasksJson["cancel"].is_object();

    if (tasksJson.contains("requests") && tasksJson["requests"].is_object())
    {
      const auto &requestsJson = tasksJson["requests"];
      if (requestsJson.contains("sampling") && requestsJson["sampling"].is_object())
      {
        tasksCapability.samplingCreateMessage = requestsJson["sampling"].contains("createMessage") && requestsJson["sampling"]["createMessage"].is_object();
      }

      if (requestsJson.contains("elicitation") && requestsJson["elicitation"].is_object())
      {
        tasksCapability.elicitationCreate = requestsJson["elicitation"].contains("create") && requestsJson["elicitation"]["create"].is_object();
      }
    }

    tasks = tasksCapability;
  }

  std::optional<jsoncons::json> experimental;
  if (capabilitiesJson.contains("experimental") && capabilitiesJson["experimental"].is_object())
  {
    experimental = capabilitiesJson["experimental"];
  }

  return {roots, sampling, elicitation, tasks, std::move(experimental)};
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static auto parseServerCapabilities(const jsoncons::json &capabilitiesJson) -> ServerCapabilities
{
  if (!capabilitiesJson.is_object())
  {
    return ServerCapabilities {};
  }

  std::optional<LoggingCapability> logging;
  if (capabilitiesJson.contains("logging") && capabilitiesJson["logging"].is_object())
  {
    logging = LoggingCapability {};
  }

  std::optional<CompletionsCapability> completions;
  if (capabilitiesJson.contains("completions") && capabilitiesJson["completions"].is_object())
  {
    completions = CompletionsCapability {};
  }

  std::optional<PromptsCapability> prompts;
  if (capabilitiesJson.contains("prompts") && capabilitiesJson["prompts"].is_object())
  {
    PromptsCapability promptsCapability;
    if (capabilitiesJson["prompts"].contains("listChanged") && capabilitiesJson["prompts"]["listChanged"].is_bool())
    {
      promptsCapability.listChanged = capabilitiesJson["prompts"]["listChanged"].as<bool>();
    }

    prompts = promptsCapability;
  }

  std::optional<ResourcesCapability> resources;
  if (capabilitiesJson.contains("resources") && capabilitiesJson["resources"].is_object())
  {
    ResourcesCapability resourcesCapability;
    if (capabilitiesJson["resources"].contains("subscribe") && capabilitiesJson["resources"]["subscribe"].is_bool())
    {
      resourcesCapability.subscribe = capabilitiesJson["resources"]["subscribe"].as<bool>();
    }

    if (capabilitiesJson["resources"].contains("listChanged") && capabilitiesJson["resources"]["listChanged"].is_bool())
    {
      resourcesCapability.listChanged = capabilitiesJson["resources"]["listChanged"].as<bool>();
    }

    resources = resourcesCapability;
  }

  std::optional<ToolsCapability> tools;
  if (capabilitiesJson.contains("tools") && capabilitiesJson["tools"].is_object())
  {
    ToolsCapability toolsCapability;
    if (capabilitiesJson["tools"].contains("listChanged") && capabilitiesJson["tools"]["listChanged"].is_bool())
    {
      toolsCapability.listChanged = capabilitiesJson["tools"]["listChanged"].as<bool>();
    }

    tools = toolsCapability;
  }

  std::optional<TasksCapability> tasks;
  if (capabilitiesJson.contains("tasks") && capabilitiesJson["tasks"].is_object())
  {
    TasksCapability tasksCapability;
    const auto &tasksJson = capabilitiesJson["tasks"];
    tasksCapability.list = tasksJson.contains("list") && tasksJson["list"].is_object();
    tasksCapability.cancel = tasksJson.contains("cancel") && tasksJson["cancel"].is_object();

    if (tasksJson.contains("requests") && tasksJson["requests"].is_object())
    {
      const auto &requestsJson = tasksJson["requests"];
      if (requestsJson.contains("tools") && requestsJson["tools"].is_object())
      {
        tasksCapability.toolsCall = requestsJson["tools"].contains("call") && requestsJson["tools"]["call"].is_object();
      }
    }

    tasks = tasksCapability;
  }

  std::optional<jsoncons::json> experimental;
  if (capabilitiesJson.contains("experimental") && capabilitiesJson["experimental"].is_object())
  {
    experimental = capabilitiesJson["experimental"];
  }

  return {logging, completions, prompts, resources, tools, tasks, std::move(experimental)};
}

static auto serverCapabilitiesToJson(const ServerCapabilities &capabilities) -> jsoncons::json
{
  jsoncons::json capabilitiesJson = jsoncons::json::object();

  if (capabilities.experimental().has_value())
  {
    capabilitiesJson["experimental"] = *capabilities.experimental();
  }

  if (capabilities.logging().has_value())
  {
    capabilitiesJson["logging"] = jsoncons::json::object();
  }

  if (capabilities.completions().has_value())
  {
    capabilitiesJson["completions"] = jsoncons::json::object();
  }

  if (capabilities.prompts().has_value())
  {
    jsoncons::json promptsJson = jsoncons::json::object();
    if (capabilities.prompts()->listChanged)
    {
      promptsJson["listChanged"] = true;
    }

    capabilitiesJson["prompts"] = std::move(promptsJson);
  }

  if (capabilities.resources().has_value())
  {
    jsoncons::json resourcesJson = jsoncons::json::object();
    if (capabilities.resources()->subscribe)
    {
      resourcesJson["subscribe"] = true;
    }

    if (capabilities.resources()->listChanged)
    {
      resourcesJson["listChanged"] = true;
    }

    capabilitiesJson["resources"] = std::move(resourcesJson);
  }

  if (capabilities.tools().has_value())
  {
    jsoncons::json toolsJson = jsoncons::json::object();
    if (capabilities.tools()->listChanged)
    {
      toolsJson["listChanged"] = true;
    }

    capabilitiesJson["tools"] = std::move(toolsJson);
  }

  if (capabilities.tasks().has_value())
  {
    jsoncons::json tasksJson = jsoncons::json::object();
    if (capabilities.tasks()->list)
    {
      tasksJson["list"] = jsoncons::json::object();
    }

    if (capabilities.tasks()->cancel)
    {
      tasksJson["cancel"] = jsoncons::json::object();
    }

    if (capabilities.tasks()->toolsCall)
    {
      jsoncons::json requestsJson = jsoncons::json::object();
      jsoncons::json toolsJson = jsoncons::json::object();
      toolsJson["call"] = jsoncons::json::object();
      requestsJson["tools"] = std::move(toolsJson);
      tasksJson["requests"] = std::move(requestsJson);
    }

    capabilitiesJson["tasks"] = std::move(tasksJson);
  }

  return capabilitiesJson;
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

auto Session::sendRequest(const std::string &method, jsonrpc::JsonValue params, RequestOptions options) -> std::future<jsonrpc::Response>
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
        pendingClientCapabilities_ = parseClientCapabilities(params["capabilities"]);
        pendingClientInfo_ = parseImplementation(params["clientInfo"], std::string(kDefaultClientName), std::string(kSdkVersion));
        state_ = SessionState::kInitializing;
      }
    }

    if (role_ == SessionRole::kServer && state_ != SessionState::kOperating && method != kPingMethod)
    {
      throw LifecycleError("Server cannot send feature requests before receiving 'notifications/initialized'");
    }
  }

  // TODO: Actually send the request through transport
  // For now, return a future that will be set when response arrives
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

auto Session::stop() -> void
{
  const std::scoped_lock lock(mutex_);
  if (state_ == SessionState::kStopped)
  {
    return;
  }
  state_ = SessionState::kStopping;
  // TODO: Cancel pending requests, close transport
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

  const ClientCapabilities clientCaps = parseClientCapabilities(params["capabilities"]);
  const Implementation clientInfo = parseImplementation(params["clientInfo"], std::string(kDefaultClientName), std::string(kSdkVersion));

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
  result["capabilities"] = serverCapabilitiesToJson(configuredServerCapabilities_);
  result["serverInfo"] = implementationToJson(configuredServerInfo_);
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

  const ServerCapabilities serverCaps = parseServerCapabilities(successResp.result["capabilities"]);
  const Implementation serverInfo = parseImplementation(successResp.result["serverInfo"], std::string(kDefaultServerName), std::string(kSdkVersion));

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
