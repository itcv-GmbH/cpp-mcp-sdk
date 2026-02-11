#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <mcp/client/client.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/schema/validator.hpp>
#include <mcp/server/prompts.hpp>
#include <mcp/server/resources.hpp>
#include <mcp/server/tools.hpp>
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
static constexpr std::string_view kToolsListMethod = "tools/list";
static constexpr std::string_view kToolsCallMethod = "tools/call";
static constexpr std::string_view kResourcesListMethod = "resources/list";
static constexpr std::string_view kResourcesReadMethod = "resources/read";
static constexpr std::string_view kResourcesTemplatesListMethod = "resources/templates/list";
static constexpr std::string_view kPromptsListMethod = "prompts/list";
static constexpr std::string_view kPromptsGetMethod = "prompts/get";

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

static auto mcpSchemaValidator() -> const schema::Validator &
{
  static const schema::Validator validator = schema::Validator::loadPinnedMcpSchema();
  return validator;
}

static auto ensureValidResultSchema(const jsonrpc::JsonValue &result, std::string_view definitionName, std::string_view method) -> void
{
  const schema::ValidationResult validationResult = mcpSchemaValidator().validateInstance(result, definitionName);
  if (!validationResult.valid)
  {
    throw std::runtime_error("Schema validation failed for '" + std::string(method) + "' result: " + schema::formatDiagnostics(validationResult));
  }
}

static auto responseToResultOrThrow(const jsonrpc::Response &response, std::string_view method) -> const jsonrpc::JsonValue &
{
  if (std::holds_alternative<jsonrpc::ErrorResponse>(response))
  {
    const auto &errorResponse = std::get<jsonrpc::ErrorResponse>(response);
    std::ostringstream stream;
    stream << "Request '" << method << "' failed with code " << errorResponse.error.code;
    if (!errorResponse.error.message.empty())
    {
      stream << ": " << errorResponse.error.message;
    }

    throw std::runtime_error(stream.str());
  }

  return std::get<jsonrpc::SuccessResponse>(response).result;
}

static auto parseCursor(const jsonrpc::JsonValue &result, std::string_view method) -> std::optional<std::string>
{
  if (!result.contains("nextCursor"))
  {
    return std::nullopt;
  }

  if (!result["nextCursor"].is_string())
  {
    throw std::runtime_error("Invalid '" + std::string(method) + "' response: nextCursor must be a string when present");
  }

  return result["nextCursor"].as<std::string>();
}

static auto parseToolDefinition(const jsonrpc::JsonValue &toolJson) -> ToolDefinition
{
  if (!toolJson.is_object() || !toolJson.contains("name") || !toolJson["name"].is_string() || !toolJson.contains("inputSchema") || !toolJson["inputSchema"].is_object())
  {
    throw std::runtime_error("Invalid tools/list response: tool definition is missing required fields");
  }

  ToolDefinition definition;
  definition.name = toolJson["name"].as<std::string>();
  definition.inputSchema = toolJson["inputSchema"];

  if (toolJson.contains("title") && toolJson["title"].is_string())
  {
    definition.title = toolJson["title"].as<std::string>();
  }

  if (toolJson.contains("description") && toolJson["description"].is_string())
  {
    definition.description = toolJson["description"].as<std::string>();
  }

  if (toolJson.contains("icons"))
  {
    definition.icons = toolJson["icons"];
  }

  if (toolJson.contains("outputSchema") && toolJson["outputSchema"].is_object())
  {
    definition.outputSchema = toolJson["outputSchema"];
  }

  if (toolJson.contains("annotations"))
  {
    definition.annotations = toolJson["annotations"];
  }

  if (toolJson.contains("execution"))
  {
    definition.execution = toolJson["execution"];
  }

  if (toolJson.contains("_meta"))
  {
    definition.metadata = toolJson["_meta"];
  }

  return definition;
}

static auto parseResourceDefinition(const jsonrpc::JsonValue &resourceJson) -> ResourceDefinition
{
  if (!resourceJson.is_object() || !resourceJson.contains("uri") || !resourceJson["uri"].is_string() || !resourceJson.contains("name") || !resourceJson["name"].is_string())
  {
    throw std::runtime_error("Invalid resources/list response: resource definition is missing required fields");
  }

  ResourceDefinition definition;
  definition.uri = resourceJson["uri"].as<std::string>();
  definition.name = resourceJson["name"].as<std::string>();

  if (resourceJson.contains("title") && resourceJson["title"].is_string())
  {
    definition.title = resourceJson["title"].as<std::string>();
  }

  if (resourceJson.contains("description") && resourceJson["description"].is_string())
  {
    definition.description = resourceJson["description"].as<std::string>();
  }

  if (resourceJson.contains("icons"))
  {
    definition.icons = resourceJson["icons"];
  }

  if (resourceJson.contains("mimeType") && resourceJson["mimeType"].is_string())
  {
    definition.mimeType = resourceJson["mimeType"].as<std::string>();
  }

  if (resourceJson.contains("size") && (resourceJson["size"].is_uint64() || resourceJson["size"].is_int64()))
  {
    definition.size = resourceJson["size"].as<std::uint64_t>();
  }

  if (resourceJson.contains("annotations"))
  {
    definition.annotations = resourceJson["annotations"];
  }

  if (resourceJson.contains("_meta"))
  {
    definition.metadata = resourceJson["_meta"];
  }

  return definition;
}

static auto parseResourceContent(const jsonrpc::JsonValue &contentJson) -> ResourceContent
{
  if (!contentJson.is_object() || !contentJson.contains("uri") || !contentJson["uri"].is_string())
  {
    throw std::runtime_error("Invalid resources/read response: content is missing required uri field");
  }

  ResourceContent content;
  content.uri = contentJson["uri"].as<std::string>();

  if (contentJson.contains("mimeType") && contentJson["mimeType"].is_string())
  {
    content.mimeType = contentJson["mimeType"].as<std::string>();
  }

  if (contentJson.contains("annotations"))
  {
    content.annotations = contentJson["annotations"];
  }

  if (contentJson.contains("_meta"))
  {
    content.metadata = contentJson["_meta"];
  }

  if (contentJson.contains("text") && contentJson["text"].is_string())
  {
    content.kind = ResourceContentKind::kText;
    content.value = contentJson["text"].as<std::string>();
    return content;
  }

  if (contentJson.contains("blob") && contentJson["blob"].is_string())
  {
    content.kind = ResourceContentKind::kBlobBase64;
    content.value = contentJson["blob"].as<std::string>();
    return content;
  }

  throw std::runtime_error("Invalid resources/read response: content must include either text or blob");
}

static auto parseResourceTemplateDefinition(const jsonrpc::JsonValue &templateJson) -> ResourceTemplateDefinition
{
  if (!templateJson.is_object() || !templateJson.contains("uriTemplate") || !templateJson["uriTemplate"].is_string() || !templateJson.contains("name")
      || !templateJson["name"].is_string())
  {
    throw std::runtime_error("Invalid resources/templates/list response: resource template is missing required fields");
  }

  ResourceTemplateDefinition definition;
  definition.uriTemplate = templateJson["uriTemplate"].as<std::string>();
  definition.name = templateJson["name"].as<std::string>();

  if (templateJson.contains("title") && templateJson["title"].is_string())
  {
    definition.title = templateJson["title"].as<std::string>();
  }

  if (templateJson.contains("description") && templateJson["description"].is_string())
  {
    definition.description = templateJson["description"].as<std::string>();
  }

  if (templateJson.contains("icons"))
  {
    definition.icons = templateJson["icons"];
  }

  if (templateJson.contains("mimeType") && templateJson["mimeType"].is_string())
  {
    definition.mimeType = templateJson["mimeType"].as<std::string>();
  }

  if (templateJson.contains("annotations"))
  {
    definition.annotations = templateJson["annotations"];
  }

  if (templateJson.contains("_meta"))
  {
    definition.metadata = templateJson["_meta"];
  }

  return definition;
}

static auto parsePromptArgument(const jsonrpc::JsonValue &argumentJson) -> PromptArgumentDefinition
{
  if (!argumentJson.is_object() || !argumentJson.contains("name") || !argumentJson["name"].is_string())
  {
    throw std::runtime_error("Invalid prompts/list response: prompt argument is missing required name field");
  }

  PromptArgumentDefinition argument;
  argument.name = argumentJson["name"].as<std::string>();

  if (argumentJson.contains("title") && argumentJson["title"].is_string())
  {
    argument.title = argumentJson["title"].as<std::string>();
  }

  if (argumentJson.contains("description") && argumentJson["description"].is_string())
  {
    argument.description = argumentJson["description"].as<std::string>();
  }

  if (argumentJson.contains("required") && argumentJson["required"].is_bool())
  {
    argument.required = argumentJson["required"].as<bool>();
  }

  if (argumentJson.contains("_meta"))
  {
    argument.metadata = argumentJson["_meta"];
  }

  return argument;
}

static auto parsePromptDefinition(const jsonrpc::JsonValue &promptJson) -> PromptDefinition
{
  if (!promptJson.is_object() || !promptJson.contains("name") || !promptJson["name"].is_string())
  {
    throw std::runtime_error("Invalid prompts/list response: prompt definition is missing required name field");
  }

  PromptDefinition definition;
  definition.name = promptJson["name"].as<std::string>();

  if (promptJson.contains("title") && promptJson["title"].is_string())
  {
    definition.title = promptJson["title"].as<std::string>();
  }

  if (promptJson.contains("description") && promptJson["description"].is_string())
  {
    definition.description = promptJson["description"].as<std::string>();
  }

  if (promptJson.contains("icons"))
  {
    definition.icons = promptJson["icons"];
  }

  if (promptJson.contains("arguments"))
  {
    if (!promptJson["arguments"].is_array())
    {
      throw std::runtime_error("Invalid prompts/list response: prompt arguments must be an array when present");
    }

    for (const auto &argumentJson : promptJson["arguments"].array_range())
    {
      definition.arguments.push_back(parsePromptArgument(argumentJson));
    }
  }

  if (promptJson.contains("_meta"))
  {
    definition.metadata = promptJson["_meta"];
  }

  return definition;
}

static auto parsePromptMessage(const jsonrpc::JsonValue &messageJson) -> PromptMessage
{
  if (!messageJson.is_object() || !messageJson.contains("role") || !messageJson["role"].is_string() || !messageJson.contains("content") || !messageJson["content"].is_object())
  {
    throw std::runtime_error("Invalid prompts/get response: message must include role and object content");
  }

  PromptMessage message;
  message.role = messageJson["role"].as<std::string>();
  message.content = messageJson["content"];
  return message;
}

static auto ensureServerCapabilityAvailable(const Client &client, std::string_view capabilityName, std::string_view method) -> void
{
  const auto negotiatedCapabilities = client.negotiatedServerCapabilities();
  if (!negotiatedCapabilities.has_value())
  {
    return;
  }

  if (negotiatedCapabilities->hasCapability(capabilityName))
  {
    return;
  }

  throw CapabilityError("Server capability '" + std::string(capabilityName) + "' is required for method '" + std::string(method) + "'");
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

auto Client::connectStdio(const transport::StdioClientOptions &options) -> void
{
  attachTransport(std::make_shared<transport::StdioTransport>(options));
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

auto Client::listTools(std::optional<std::string> cursor, RequestOptions options) -> ListToolsResult
{
  ensureServerCapabilityAvailable(*this, "tools", kToolsListMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  if (cursor.has_value())
  {
    params["cursor"] = *cursor;
  }

  const jsonrpc::Response response = sendRequest(std::string(kToolsListMethod), std::move(params), options).get();
  const jsonrpc::JsonValue &result = responseToResultOrThrow(response, kToolsListMethod);
  ensureValidResultSchema(result, "ListToolsResult", kToolsListMethod);

  if (!result.contains("tools") || !result["tools"].is_array())
  {
    throw std::runtime_error("Invalid tools/list response: tools must be an array");
  }

  ListToolsResult parsedResult;
  parsedResult.nextCursor = parseCursor(result, kToolsListMethod);
  parsedResult.tools.reserve(result["tools"].size());
  for (const auto &toolJson : result["tools"].array_range())
  {
    parsedResult.tools.push_back(parseToolDefinition(toolJson));
  }

  return parsedResult;
}

auto Client::callTool(const std::string &name, jsonrpc::JsonValue arguments, RequestOptions options) -> CallToolResult
{
  ensureServerCapabilityAvailable(*this, "tools", kToolsCallMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  params["name"] = name;
  params["arguments"] = std::move(arguments);

  const jsonrpc::Response response = sendRequest(std::string(kToolsCallMethod), std::move(params), options).get();
  const jsonrpc::JsonValue &result = responseToResultOrThrow(response, kToolsCallMethod);
  ensureValidResultSchema(result, "CallToolResult", kToolsCallMethod);

  if (!result.contains("content") || !result["content"].is_array())
  {
    throw std::runtime_error("Invalid tools/call response: content must be an array");
  }

  CallToolResult parsedResult;
  parsedResult.content = result["content"];

  if (result.contains("structuredContent"))
  {
    parsedResult.structuredContent = result["structuredContent"];
  }

  if (result.contains("isError") && result["isError"].is_bool())
  {
    parsedResult.isError = result["isError"].as<bool>();
  }

  if (result.contains("_meta"))
  {
    parsedResult.metadata = result["_meta"];
  }

  return parsedResult;
}

auto Client::listResources(std::optional<std::string> cursor, RequestOptions options) -> ListResourcesResult
{
  ensureServerCapabilityAvailable(*this, "resources", kResourcesListMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  if (cursor.has_value())
  {
    params["cursor"] = *cursor;
  }

  const jsonrpc::Response response = sendRequest(std::string(kResourcesListMethod), std::move(params), options).get();
  const jsonrpc::JsonValue &result = responseToResultOrThrow(response, kResourcesListMethod);
  ensureValidResultSchema(result, "ListResourcesResult", kResourcesListMethod);

  if (!result.contains("resources") || !result["resources"].is_array())
  {
    throw std::runtime_error("Invalid resources/list response: resources must be an array");
  }

  ListResourcesResult parsedResult;
  parsedResult.nextCursor = parseCursor(result, kResourcesListMethod);
  parsedResult.resources.reserve(result["resources"].size());
  for (const auto &resourceJson : result["resources"].array_range())
  {
    parsedResult.resources.push_back(parseResourceDefinition(resourceJson));
  }

  return parsedResult;
}

auto Client::readResource(const std::string &uri, RequestOptions options) -> ReadResourceResult
{
  ensureServerCapabilityAvailable(*this, "resources", kResourcesReadMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  params["uri"] = uri;

  const jsonrpc::Response response = sendRequest(std::string(kResourcesReadMethod), std::move(params), options).get();
  const jsonrpc::JsonValue &result = responseToResultOrThrow(response, kResourcesReadMethod);
  ensureValidResultSchema(result, "ReadResourceResult", kResourcesReadMethod);

  if (!result.contains("contents") || !result["contents"].is_array())
  {
    throw std::runtime_error("Invalid resources/read response: contents must be an array");
  }

  ReadResourceResult parsedResult;
  parsedResult.contents.reserve(result["contents"].size());
  for (const auto &contentJson : result["contents"].array_range())
  {
    parsedResult.contents.push_back(parseResourceContent(contentJson));
  }

  return parsedResult;
}

auto Client::listResourceTemplates(std::optional<std::string> cursor, RequestOptions options) -> ListResourceTemplatesResult
{
  ensureServerCapabilityAvailable(*this, "resources", kResourcesTemplatesListMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  if (cursor.has_value())
  {
    params["cursor"] = *cursor;
  }

  const jsonrpc::Response response = sendRequest(std::string(kResourcesTemplatesListMethod), std::move(params), options).get();
  const jsonrpc::JsonValue &result = responseToResultOrThrow(response, kResourcesTemplatesListMethod);
  ensureValidResultSchema(result, "ListResourceTemplatesResult", kResourcesTemplatesListMethod);

  if (!result.contains("resourceTemplates") || !result["resourceTemplates"].is_array())
  {
    throw std::runtime_error("Invalid resources/templates/list response: resourceTemplates must be an array");
  }

  ListResourceTemplatesResult parsedResult;
  parsedResult.nextCursor = parseCursor(result, kResourcesTemplatesListMethod);
  parsedResult.resourceTemplates.reserve(result["resourceTemplates"].size());
  for (const auto &templateJson : result["resourceTemplates"].array_range())
  {
    parsedResult.resourceTemplates.push_back(parseResourceTemplateDefinition(templateJson));
  }

  return parsedResult;
}

auto Client::listPrompts(std::optional<std::string> cursor, RequestOptions options) -> ListPromptsResult
{
  ensureServerCapabilityAvailable(*this, "prompts", kPromptsListMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  if (cursor.has_value())
  {
    params["cursor"] = *cursor;
  }

  const jsonrpc::Response response = sendRequest(std::string(kPromptsListMethod), std::move(params), options).get();
  const jsonrpc::JsonValue &result = responseToResultOrThrow(response, kPromptsListMethod);
  ensureValidResultSchema(result, "ListPromptsResult", kPromptsListMethod);

  if (!result.contains("prompts") || !result["prompts"].is_array())
  {
    throw std::runtime_error("Invalid prompts/list response: prompts must be an array");
  }

  ListPromptsResult parsedResult;
  parsedResult.nextCursor = parseCursor(result, kPromptsListMethod);
  parsedResult.prompts.reserve(result["prompts"].size());
  for (const auto &promptJson : result["prompts"].array_range())
  {
    parsedResult.prompts.push_back(parsePromptDefinition(promptJson));
  }

  return parsedResult;
}

auto Client::getPrompt(const std::string &name, jsonrpc::JsonValue arguments, RequestOptions options) -> PromptGetResult
{
  ensureServerCapabilityAvailable(*this, "prompts", kPromptsGetMethod);

  jsonrpc::JsonValue params = jsonrpc::JsonValue::object();
  params["name"] = name;
  params["arguments"] = std::move(arguments);

  const jsonrpc::Response response = sendRequest(std::string(kPromptsGetMethod), std::move(params), options).get();
  const jsonrpc::JsonValue &result = responseToResultOrThrow(response, kPromptsGetMethod);
  ensureValidResultSchema(result, "GetPromptResult", kPromptsGetMethod);

  if (!result.contains("messages") || !result["messages"].is_array())
  {
    throw std::runtime_error("Invalid prompts/get response: messages must be an array");
  }

  PromptGetResult parsedResult;

  if (result.contains("description") && result["description"].is_string())
  {
    parsedResult.description = result["description"].as<std::string>();
  }

  parsedResult.messages.reserve(result["messages"].size());
  for (const auto &messageJson : result["messages"].array_range())
  {
    parsedResult.messages.push_back(parsePromptMessage(messageJson));
  }

  if (result.contains("_meta"))
  {
    parsedResult.metadata = result["_meta"];
  }

  return parsedResult;
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

  const bool isInitializeRequest = request.method == kInitializeMethod;

  if (isInitializeRequest)
  {
    applyInitializeDefaults(request);
  }

  const jsonrpc::JsonValue lifecycleParams = request.params.has_value() ? *request.params : jsonrpc::JsonValue::object();
  static_cast<void>(session_->sendRequest(request.method, lifecycleParams, options));

  {
    const std::scoped_lock lock(mutex_);
    if (isInitializeRequest)
    {
      pendingInitializeRequestId_ = request.id;
    }
  }

  jsonrpc::OutboundRequestOptions outboundOptions;
  outboundOptions.timeout = options.timeout;
  outboundOptions.cancelOnTimeout = options.cancelOnTimeout;
  auto responseFuture = router_.sendRequest(jsonrpc::RequestContext {}, std::move(request), std::move(outboundOptions));

  if (isInitializeRequest)
  {
    const auto status = responseFuture.wait_for(std::chrono::milliseconds {0});
    if (status == std::future_status::ready)
    {
      bool initializeResponseWasAlreadyHandled = false;
      {
        const std::scoped_lock lock(mutex_);
        initializeResponseWasAlreadyHandled = !pendingInitializeRequestId_.has_value();
      }

      if (!initializeResponseWasAlreadyHandled)
      {
        jsonrpc::Response response = responseFuture.get();

        {
          const std::scoped_lock lock(mutex_);
          pendingInitializeRequestId_.reset();
        }

        try
        {
          session_->handleInitializeResponse(response);
        }
        catch (const LifecycleError &error)
        {
          static_cast<void>(error);
        }

        return makeReadyResponseFuture(std::move(response));
      }
    }
  }

  return responseFuture;
}

auto Client::sendRequestAsync(std::string method, jsonrpc::JsonValue params, const ResponseCallback &callback, RequestOptions options) -> void
{
  auto responseFuture = sendRequest(std::move(method), std::move(params), options);
  std::thread([callback, responseFuture = std::move(responseFuture)]() mutable -> void { callback(responseFuture.get()); }).detach();
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
