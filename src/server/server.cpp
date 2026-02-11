#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/server.hpp>
#include <mcp/version.hpp>

namespace mcp
{
namespace detail
{

constexpr std::string_view kInitializeMethod = "initialize";
constexpr std::string_view kPingMethod = "ping";
constexpr std::string_view kInitializedNotificationMethod = "notifications/initialized";
constexpr std::string_view kMessageNotificationMethod = "notifications/message";
constexpr std::string_view kLoggingSetLevelMethod = "logging/setLevel";
constexpr std::string_view kCompletionCompleteMethod = "completion/complete";
constexpr std::string_view kDefaultServerName = "mcp-cpp-sdk";
constexpr std::string_view kCursorPrefix = "mcp:v1:";

constexpr std::array<std::pair<std::string_view, LogLevel>, 8> kLogLevelMappings {
  std::pair<std::string_view, LogLevel> {"debug", LogLevel::kDebug},
  std::pair<std::string_view, LogLevel> {"info", LogLevel::kInfo},
  std::pair<std::string_view, LogLevel> {"notice", LogLevel::kNotice},
  std::pair<std::string_view, LogLevel> {"warning", LogLevel::kWarning},
  std::pair<std::string_view, LogLevel> {"error", LogLevel::kError},
  std::pair<std::string_view, LogLevel> {"critical", LogLevel::kCritical},
  std::pair<std::string_view, LogLevel> {"alert", LogLevel::kAlert},
  std::pair<std::string_view, LogLevel> {"emergency", LogLevel::kEmergency},
};

auto makeReadyResponseFuture(jsonrpc::Response response) -> std::future<jsonrpc::Response>
{
  std::promise<jsonrpc::Response> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

auto makePingResponse(const jsonrpc::RequestId &requestId) -> jsonrpc::Response
{
  jsonrpc::SuccessResponse response;
  response.id = requestId;
  response.result = jsonrpc::JsonValue::object();
  return response;
}

auto makeMethodNotFoundResponse(const jsonrpc::RequestId &requestId, std::string_view method) -> jsonrpc::Response
{
  const std::string message = "Method '" + std::string(method) + "' is not available for the negotiated server capabilities.";
  return jsonrpc::makeErrorResponse(jsonrpc::makeMethodNotFoundError(std::nullopt, message), requestId);
}

auto makeLifecycleRejectedResponse(const jsonrpc::RequestId &requestId, std::string_view method) -> jsonrpc::Response
{
  const std::string message = "Method '" + std::string(method) + "' is not valid before initialization completes.";
  return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidRequestError(std::nullopt, message), requestId);
}

auto defaultServerInfo() -> Implementation
{
  return {std::string(detail::kDefaultServerName), std::string(kSdkVersion)};
}

auto endpointToCursorLabel(ListEndpoint endpoint) -> std::string_view
{
  switch (endpoint)
  {
    case ListEndpoint::kTools:
      return "tools";
    case ListEndpoint::kResources:
      return "resources";
    case ListEndpoint::kPrompts:
      return "prompts";
    case ListEndpoint::kTasks:
      return "tasks";
  }

  throw std::invalid_argument("Unsupported list endpoint");
}

auto isValidCursorChar(char value) -> bool
{
  return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '-' || value == '_';
}

auto encodeCursorPayload(std::string_view payload) -> std::string
{
  constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string encoded;
  encoded.reserve(((payload.size() + 2) / 3) * 4);

  std::size_t index = 0;
  while (index + 3 <= payload.size())
  {
    const std::uint32_t chunk = (static_cast<std::uint32_t>(static_cast<unsigned char>(payload[index])) << 16U)
      | (static_cast<std::uint32_t>(static_cast<unsigned char>(payload[index + 1])) << 8U) | static_cast<std::uint32_t>(static_cast<unsigned char>(payload[index + 2]));

    encoded.push_back(alphabet[(chunk >> 18U) & 0x3FU]);
    encoded.push_back(alphabet[(chunk >> 12U) & 0x3FU]);
    encoded.push_back(alphabet[(chunk >> 6U) & 0x3FU]);
    encoded.push_back(alphabet[chunk & 0x3FU]);
    index += 3;
  }

  const std::size_t remaining = payload.size() - index;
  if (remaining == 1)
  {
    const std::uint32_t chunk = static_cast<std::uint32_t>(static_cast<unsigned char>(payload[index])) << 16U;
    encoded.push_back(alphabet[(chunk >> 18U) & 0x3FU]);
    encoded.push_back(alphabet[(chunk >> 12U) & 0x3FU]);
  }
  else if (remaining == 2)
  {
    const std::uint32_t chunk =
      (static_cast<std::uint32_t>(static_cast<unsigned char>(payload[index])) << 16U) | (static_cast<std::uint32_t>(static_cast<unsigned char>(payload[index + 1])) << 8U);
    encoded.push_back(alphabet[(chunk >> 18U) & 0x3FU]);
    encoded.push_back(alphabet[(chunk >> 12U) & 0x3FU]);
    encoded.push_back(alphabet[(chunk >> 6U) & 0x3FU]);
  }

  return encoded;
}

auto decodeCursorPayload(std::string_view encoded) -> std::optional<std::string>
{
  if (encoded.empty())
  {
    return std::nullopt;
  }

  auto decodeChar = [](char value) -> std::optional<std::uint32_t>
  {
    if (value >= 'A' && value <= 'Z')
    {
      return static_cast<std::uint32_t>(value - 'A');
    }
    if (value >= 'a' && value <= 'z')
    {
      return static_cast<std::uint32_t>(value - 'a') + 26U;
    }
    if (value >= '0' && value <= '9')
    {
      return static_cast<std::uint32_t>(value - '0') + 52U;
    }
    if (value == '-')
    {
      return 62U;
    }
    if (value == '_')
    {
      return 63U;
    }

    return std::nullopt;
  };

  const std::size_t remainder = encoded.size() % 4;
  if (remainder == 1)
  {
    return std::nullopt;
  }

  std::string decoded;
  decoded.reserve((encoded.size() * 3) / 4);

  std::size_t index = 0;
  while (index + 4 <= encoded.size())
  {
    const auto a = decodeChar(encoded[index]);
    const auto b = decodeChar(encoded[index + 1]);
    const auto c = decodeChar(encoded[index + 2]);
    const auto d = decodeChar(encoded[index + 3]);
    if (!a.has_value() || !b.has_value() || !c.has_value() || !d.has_value())
    {
      return std::nullopt;
    }

    const std::uint32_t chunk = (*a << 18U) | (*b << 12U) | (*c << 6U) | *d;
    decoded.push_back(static_cast<char>((chunk >> 16U) & 0xFFU));
    decoded.push_back(static_cast<char>((chunk >> 8U) & 0xFFU));
    decoded.push_back(static_cast<char>(chunk & 0xFFU));
    index += 4;
  }

  if (remainder == 2)
  {
    const auto a = decodeChar(encoded[index]);
    const auto b = decodeChar(encoded[index + 1]);
    if (!a.has_value() || !b.has_value())
    {
      return std::nullopt;
    }

    const std::uint32_t chunk = (*a << 18U) | (*b << 12U);
    decoded.push_back(static_cast<char>((chunk >> 16U) & 0xFFU));
  }
  else if (remainder == 3)
  {
    const auto a = decodeChar(encoded[index]);
    const auto b = decodeChar(encoded[index + 1]);
    const auto c = decodeChar(encoded[index + 2]);
    if (!a.has_value() || !b.has_value() || !c.has_value())
    {
      return std::nullopt;
    }

    const std::uint32_t chunk = (*a << 18U) | (*b << 12U) | (*c << 6U);
    decoded.push_back(static_cast<char>((chunk >> 16U) & 0xFFU));
    decoded.push_back(static_cast<char>((chunk >> 8U) & 0xFFU));
  }

  return decoded;
}

auto makePaginationCursor(ListEndpoint endpoint, std::size_t startIndex) -> std::string
{
  const std::string payload = std::string(kCursorPrefix) + std::string(endpointToCursorLabel(endpoint)) + ":" + std::to_string(startIndex);
  return encodeCursorPayload(payload);
}

auto parsePaginationCursor(ListEndpoint endpoint, std::string_view cursor) -> std::optional<std::size_t>
{
  if (cursor.empty())
  {
    return std::nullopt;
  }

  if (!std::all_of(cursor.begin(), cursor.end(), isValidCursorChar))
  {
    return std::nullopt;
  }

  const auto decoded = decodeCursorPayload(cursor);
  if (!decoded.has_value())
  {
    return std::nullopt;
  }

  const std::string expectedPrefix = std::string(kCursorPrefix) + std::string(endpointToCursorLabel(endpoint)) + ":";
  if (decoded->rfind(expectedPrefix, 0) != 0)
  {
    return std::nullopt;
  }

  const std::string indexText = decoded->substr(expectedPrefix.size());
  if (indexText.empty())
  {
    return std::nullopt;
  }

  std::size_t consumed = 0;
  std::uint64_t parsedIndex = 0;
  try
  {
    parsedIndex = std::stoull(indexText, &consumed);
  }
  catch (const std::exception &)
  {
    return std::nullopt;
  }

  if (consumed != indexText.size() || parsedIndex > std::numeric_limits<std::size_t>::max())
  {
    return std::nullopt;
  }

  return static_cast<std::size_t>(parsedIndex);
}

auto logLevelToString(LogLevel level) -> std::string_view
{
  for (const auto &[name, mappedLevel] : kLogLevelMappings)
  {
    if (mappedLevel == level)
    {
      return name;
    }
  }

  return "debug";
}

auto parseLogLevel(std::string_view level) -> std::optional<LogLevel>
{
  for (const auto &[name, mappedLevel] : kLogLevelMappings)
  {
    if (level == name)
    {
      return mappedLevel;
    }
  }

  return std::nullopt;
}

auto logLevelWeight(LogLevel level) -> int
{
  switch (level)
  {
    case LogLevel::kDebug:
      return 0;
    case LogLevel::kInfo:
      return 1;
    case LogLevel::kNotice:
      return 2;
    case LogLevel::kWarning:
      return 3;
    case LogLevel::kError:
      return 4;
    case LogLevel::kCritical:
      return 5;
    case LogLevel::kAlert:
      return 6;
    case LogLevel::kEmergency:
      return 7;
  }

  return 0;
}

auto capabilityForMethod(std::string_view method) -> std::optional<std::string_view>
{
  if (method == "tools/list" || method == "tools/call")
  {
    return "tools";
  }

  if (method == "resources/list" || method == "resources/read" || method == "resources/templates/list" || method == "resources/subscribe" || method == "resources/unsubscribe")
  {
    return "resources";
  }

  if (method == "prompts/list" || method == "prompts/get")
  {
    return "prompts";
  }

  if (method == "completion/complete")
  {
    return "completions";
  }

  if (method == "logging/setLevel")
  {
    return "logging";
  }

  if (method == "tasks/get" || method == "tasks/result" || method == "tasks/list" || method == "tasks/cancel")
  {
    return "tasks";
  }

  return std::nullopt;
}

}  // namespace detail

auto Server::create(SessionOptions options) -> std::shared_ptr<Server>
{
  ServerConfiguration configuration;
  configuration.sessionOptions = std::move(options);
  return create(std::move(configuration));
}

auto Server::create(ServerConfiguration configuration) -> std::shared_ptr<Server>
{
  auto session = std::make_shared<Session>(configuration.sessionOptions);
  return std::make_shared<Server>(std::move(session), std::move(configuration));
}

Server::Server(std::shared_ptr<Session> session)
  : Server(std::move(session), ServerConfiguration {})
{
}

Server::Server(std::shared_ptr<Session> session, ServerConfiguration configuration)
  : session_(std::move(session))
  , configuration_(std::move(configuration))
{
  if (!session_)
  {
    throw std::invalid_argument("Server requires a non-null session");
  }

  session_->setRole(SessionRole::kServer);

  if (!configuration_.serverInfo.has_value())
  {
    configuration_.serverInfo = detail::defaultServerInfo();
  }

  configureSessionInitialization();
  registerCoreHandlers();
}

auto Server::configuration() const noexcept -> const ServerConfiguration &
{
  return configuration_;
}

auto Server::session() const noexcept -> const std::shared_ptr<Session> &
{
  return session_;
}

auto Server::start() -> void
{
  session_->setRole(SessionRole::kServer);
  session_->start();
}

auto Server::stop() -> void
{
  session_->stop();
}

auto Server::registerRequestHandler(std::string method, jsonrpc::RequestHandler handler) -> void
{
  if (handler == nullptr)
  {
    throw std::invalid_argument("Request handler must not be null");
  }

  if (isCoreRequestMethod(method))
  {
    throw std::invalid_argument("Core request handlers are managed by Server");
  }

  const std::string methodName = method;
  router_.registerRequestHandler(std::move(method),
                                 [this, methodName, handler = std::move(handler)](
                                   const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>  // NOLINT(bugprone-exception-escape)
                                 {
                                   if (!isMethodEnabledByCapability(methodName))
                                   {
                                     return detail::makeReadyResponseFuture(detail::makeMethodNotFoundResponse(request.id, methodName));
                                   }

                                   return handler(context, request);
                                 });
}

auto Server::registerNotificationHandler(std::string method, jsonrpc::NotificationHandler handler) -> void
{
  if (handler == nullptr)
  {
    throw std::invalid_argument("Notification handler must not be null");
  }

  if (method == detail::kInitializedNotificationMethod)
  {
    throw std::invalid_argument("Core notification handlers are managed by Server");
  }

  router_.registerNotificationHandler(std::move(method), std::move(handler));
}

auto Server::setOutboundMessageSender(jsonrpc::OutboundMessageSender sender) -> void
{
  router_.setOutboundMessageSender(std::move(sender));
}

auto Server::handleRequest(const jsonrpc::RequestContext &context, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
{
  if (!session_->canHandleRequest(request.method))
  {
    return detail::makeReadyResponseFuture(detail::makeLifecycleRejectedResponse(request.id, request.method));
  }

  if (!isMethodEnabledByCapability(request.method))
  {
    return detail::makeReadyResponseFuture(detail::makeMethodNotFoundResponse(request.id, request.method));
  }

  return router_.dispatchRequest(context, request);
}

auto Server::handleNotification(const jsonrpc::RequestContext &context, const jsonrpc::Notification &notification) -> void
{
  router_.dispatchNotification(context, notification);
}

auto Server::handleResponse(const jsonrpc::RequestContext &context, const jsonrpc::Response &response) -> bool
{
  return router_.dispatchResponse(context, response);
}

auto Server::sendRequest(const jsonrpc::RequestContext &context, jsonrpc::Request request, jsonrpc::OutboundRequestOptions options) -> std::future<jsonrpc::Response>
{
  return router_.sendRequest(context, std::move(request), std::move(options));
}

auto Server::sendNotification(const jsonrpc::RequestContext &context, jsonrpc::Notification notification) -> void
{
  router_.sendNotification(context, std::move(notification));
}

auto Server::setCompletionHandler(CompletionHandler handler) -> void
{
  const std::scoped_lock lock(utilityMutex_);
  completionHandler_ = std::move(handler);
}

auto Server::emitLogMessage(const jsonrpc::RequestContext &context, LogLevel level, jsonrpc::JsonValue data, std::optional<std::string> logger) -> bool
{
  if (!configuration_.capabilities.hasCapability("logging"))
  {
    return false;
  }

  LogLevel configuredLevel = LogLevel::kDebug;
  {
    const std::scoped_lock lock(utilityMutex_);
    configuredLevel = logLevel_;
  }

  if (detail::logLevelWeight(level) < detail::logLevelWeight(configuredLevel))
  {
    return false;
  }

  jsonrpc::Notification notification;
  notification.method = std::string(detail::kMessageNotificationMethod);
  notification.params = jsonrpc::JsonValue::object();
  (*notification.params)["level"] = std::string(detail::logLevelToString(level));
  if (logger.has_value())
  {
    (*notification.params)["logger"] = std::move(*logger);
  }
  (*notification.params)["data"] = std::move(data);

  sendNotification(context, std::move(notification));
  return true;
}

auto Server::logLevel() const -> LogLevel
{
  const std::scoped_lock lock(utilityMutex_);
  return logLevel_;
}

auto Server::paginateList(ListEndpoint endpoint, const std::optional<std::string> &cursor, std::size_t totalItems, std::size_t pageSize) -> PaginationWindow
{
  if (pageSize == 0)
  {
    throw std::invalid_argument("Pagination page size must be greater than zero");
  }

  std::size_t startIndex = 0;
  if (cursor.has_value())
  {
    const auto parsedCursor = detail::parsePaginationCursor(endpoint, *cursor);
    if (!parsedCursor.has_value() || *parsedCursor > totalItems)
    {
      throw std::invalid_argument("Invalid pagination cursor");
    }

    startIndex = *parsedCursor;
  }

  const std::size_t endIndex = std::min(startIndex + pageSize, totalItems);

  PaginationWindow window;
  window.startIndex = startIndex;
  window.endIndex = endIndex;
  if (endIndex < totalItems)
  {
    window.nextCursor = detail::makePaginationCursor(endpoint, endIndex);
  }

  return window;
}

auto Server::configureSessionInitialization() -> void
{
  if (!configuration_.serverInfo.has_value())
  {
    configuration_.serverInfo = detail::defaultServerInfo();
  }

  session_->configureServerInitialization(configuration_.capabilities, *configuration_.serverInfo, configuration_.instructions);
}

auto Server::registerCoreHandlers() -> void
{
  router_.registerRequestHandler(std::string(detail::kInitializeMethod),
                                 [this](const jsonrpc::RequestContext &, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(session_->handleInitializeRequest(request)); });

  router_.registerRequestHandler(std::string(detail::kPingMethod),
                                 [](const jsonrpc::RequestContext &, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(detail::makePingResponse(request.id)); });

  router_.registerNotificationHandler(std::string(detail::kInitializedNotificationMethod),
                                      [this](const jsonrpc::RequestContext &, const jsonrpc::Notification &) -> void { session_->handleInitializedNotification(); });

  router_.registerRequestHandler(std::string(detail::kLoggingSetLevelMethod),
                                 [this](const jsonrpc::RequestContext &, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleLoggingSetLevelRequest(request)); });

  router_.registerRequestHandler(std::string(detail::kCompletionCompleteMethod),
                                 [this](const jsonrpc::RequestContext &, const jsonrpc::Request &request) -> std::future<jsonrpc::Response>
                                 { return detail::makeReadyResponseFuture(handleCompletionCompleteRequest(request)); });
}

auto Server::handleLoggingSetLevelRequest(const jsonrpc::Request &request) -> jsonrpc::Response
{
  if (!request.params.has_value() || !request.params->is_object())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "logging/setLevel requires params object"), request.id);
  }

  const auto &params = *request.params;
  if (!params.contains("level") || !params["level"].is_string())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "logging/setLevel requires string params.level"), request.id);
  }

  const auto parsedLevel = detail::parseLogLevel(params["level"].as<std::string>());
  if (!parsedLevel.has_value())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "Invalid log level"), request.id);
  }

  {
    const std::scoped_lock lock(utilityMutex_);
    logLevel_ = *parsedLevel;
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = jsonrpc::JsonValue::object();
  return response;
}

auto Server::handleCompletionCompleteRequest(const jsonrpc::Request &request) -> jsonrpc::Response
{
  if (!request.params.has_value() || !request.params->is_object())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "completion/complete requires params object"), request.id);
  }

  const auto &params = *request.params;

  if (!params.contains("ref") || !params["ref"].is_object())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "completion/complete requires object params.ref"), request.id);
  }

  if (!params.contains("argument") || !params["argument"].is_object())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "completion/complete requires object params.argument"), request.id);
  }

  CompletionRequest completionRequest;
  const auto &ref = params["ref"];
  if (!ref.contains("type") || !ref["type"].is_string())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "completion/complete requires string params.ref.type"), request.id);
  }

  const std::string referenceType = ref["type"].as<std::string>();
  if (referenceType == "ref/prompt")
  {
    if (!ref.contains("name") || !ref["name"].is_string())
    {
      return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "ref/prompt requires params.ref.name"), request.id);
    }

    completionRequest.referenceType = CompletionReferenceType::kPrompt;
    completionRequest.referenceValue = ref["name"].as<std::string>();
  }
  else if (referenceType == "ref/resource")
  {
    if (!ref.contains("uri") || !ref["uri"].is_string())
    {
      return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "ref/resource requires params.ref.uri"), request.id);
    }

    completionRequest.referenceType = CompletionReferenceType::kResource;
    completionRequest.referenceValue = ref["uri"].as<std::string>();
  }
  else
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "Unsupported completion reference type"), request.id);
  }

  const auto &argument = params["argument"];
  if (!argument.contains("name") || !argument["name"].is_string() || !argument.contains("value") || !argument["value"].is_string())
  {
    return jsonrpc::makeErrorResponse(jsonrpc::makeInvalidParamsError(std::nullopt, "completion/complete requires string params.argument.name and params.argument.value"),
                                      request.id);
  }

  completionRequest.argumentName = argument["name"].as<std::string>();
  completionRequest.argumentValue = argument["value"].as<std::string>();

  if (params.contains("context") && params["context"].is_object() && params["context"].contains("arguments") && params["context"]["arguments"].is_object())
  {
    completionRequest.contextArguments = params["context"]["arguments"];
  }

  CompletionHandler completionHandler;
  {
    const std::scoped_lock lock(utilityMutex_);
    completionHandler = completionHandler_;
  }

  CompletionResult completionResult;
  if (completionHandler != nullptr)
  {
    try
    {
      completionResult = completionHandler(completionRequest);
    }
    catch (const std::exception &exception)
    {
      return jsonrpc::makeErrorResponse(jsonrpc::makeInternalError(std::nullopt, std::string("completion/complete failed: ") + exception.what()), request.id);
    }
  }

  const bool truncated = completionResult.values.size() > 100;
  if (truncated)
  {
    completionResult.values.resize(100);
  }

  jsonrpc::JsonValue completionJson = jsonrpc::JsonValue::object();
  completionJson["values"] = jsonrpc::JsonValue::array();
  for (const auto &value : completionResult.values)
  {
    completionJson["values"].push_back(value);
  }

  if (completionResult.total.has_value())
  {
    completionJson["total"] = static_cast<std::uint64_t>(*completionResult.total);
  }

  if (completionResult.hasMore.has_value())
  {
    completionJson["hasMore"] = *completionResult.hasMore || truncated;
  }
  else if (truncated)
  {
    completionJson["hasMore"] = true;
  }

  jsonrpc::SuccessResponse response;
  response.id = request.id;
  response.result = jsonrpc::JsonValue::object();
  response.result["completion"] = std::move(completionJson);
  return response;
}

auto Server::isMethodEnabledByCapability(std::string_view method) const -> bool
{
  const auto capability = detail::capabilityForMethod(method);
  if (!capability.has_value())
  {
    return true;
  }

  return configuration_.capabilities.hasCapability(*capability);
}

auto Server::isCoreRequestMethod(std::string_view method) -> bool
{
  return method == detail::kInitializeMethod || method == detail::kPingMethod;
}

}  // namespace mcp
