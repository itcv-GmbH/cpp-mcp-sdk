#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <mcp/http/sse.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/transport/http.hpp>

namespace mcp::transport
{
namespace http
{
enum class StreamKind : std::uint8_t
{
  Get,
  Post,
};

static constexpr std::uint16_t kStatusOk = 200;
static constexpr std::uint16_t kStatusNoContent = 204;
static constexpr std::uint16_t kStatusAccepted = 202;
static constexpr std::uint16_t kStatusBadRequest = 400;
static constexpr std::uint16_t kStatusNotFound = 404;
static constexpr std::uint16_t kStatusMethodNotAllowed = 405;
static constexpr std::uint16_t kStatusInternalServerError = 500;
static constexpr bool kTerminateStream = true;
static constexpr bool kKeepStreamOpen = false;

struct StreamEventRecord
{
  std::uint64_t cursor = 0;
  mcp::http::sse::Event event;
};

struct StreamState
{
  std::string streamId;
  std::optional<std::string> sessionId;
  StreamKind kind = StreamKind::Get;
  std::uint64_t nextCursor = 1;
  bool terminated = false;
  std::vector<StreamEventRecord> events;
};

static auto sessionKey(const std::optional<std::string> &sessionId) -> std::string
{
  return sessionId.has_value() ? *sessionId : std::string();
}

static auto makeJsonRpcErrorBody(std::string message) -> std::string
{
  return jsonrpc::serializeMessage(jsonrpc::Message {jsonrpc::makeUnknownIdErrorResponse(jsonrpc::makeInvalidRequestError(std::nullopt, std::move(message)))});
}

static auto makeEvent(std::string eventId, std::string data) -> mcp::http::sse::Event
{
  mcp::http::sse::Event event;
  event.id = std::move(eventId);
  event.data = std::move(data);
  return event;
}

static auto makeRetryGuidanceEvent(std::uint32_t retryMilliseconds) -> mcp::http::sse::Event
{
  mcp::http::sse::Event event;
  event.retryMilliseconds = retryMilliseconds;
  event.data = "";
  return event;
}

static auto isResponseMessage(const jsonrpc::Message &message) -> bool
{
  return std::holds_alternative<jsonrpc::SuccessResponse>(message) || std::holds_alternative<jsonrpc::ErrorResponse>(message);
}

static auto isInitializeRequest(const jsonrpc::Message &message) -> bool
{
  if (!std::holds_alternative<jsonrpc::Request>(message))
  {
    return false;
  }

  return std::get<jsonrpc::Request>(message).method == "initialize";
}

static auto toRequestContext(const RequestValidationResult &validation) -> jsonrpc::RequestContext
{
  jsonrpc::RequestContext context;
  context.protocolVersion = validation.effectiveProtocolVersion;
  context.sessionId = validation.sessionId;
  return context;
}

struct StreamableHttpServer::Impl
{
  explicit Impl(StreamableHttpServerOptions options)
    : options(std::move(options))
  {
  }

  auto upsertSession(std::string sessionId, SessionLookupState state, std::optional<std::string> negotiatedProtocolVersion) -> void
  {
    SessionResolution resolution;
    resolution.state = state;
    resolution.negotiatedProtocolVersion = std::move(negotiatedProtocolVersion);
    sessions[std::move(sessionId)] = std::move(resolution);
  }

  auto sessionResolver(std::string_view sessionId) const -> SessionResolution
  {
    const auto session = sessions.find(std::string(sessionId));
    if (session == sessions.end())
    {
      return {};
    }

    return session->second;
  }

  static auto appendPrimingEvent(StreamState &stream) -> void
  {
    const std::uint64_t cursor = stream.nextCursor++;
    std::string eventId = mcp::http::sse::makeEventId(stream.streamId, cursor);
    stream.events.push_back(StreamEventRecord {cursor, makeEvent(std::move(eventId), "")});
  }

  static auto appendMessageEvent(StreamState &stream, const jsonrpc::Message &message) -> void
  {
    const std::uint64_t cursor = stream.nextCursor++;
    std::string eventId = mcp::http::sse::makeEventId(stream.streamId, cursor);
    stream.events.push_back(StreamEventRecord {cursor, makeEvent(std::move(eventId), jsonrpc::serializeMessage(message))});
  }

  auto createStream(std::optional<std::string> sessionId, StreamKind kind) -> StreamState &
  {
    const std::string streamId = "s" + std::to_string(++nextStreamOrdinal);

    StreamState stream;
    stream.streamId = streamId;
    stream.sessionId = std::move(sessionId);
    stream.kind = kind;
    appendPrimingEvent(stream);

    streamOrder.push_back(streamId);
    const auto inserted = streams.emplace(streamId, std::move(stream));
    return inserted.first->second;
  }

  static auto replayFromCursor(const StreamState &stream, std::uint64_t cursor) -> std::vector<mcp::http::sse::Event>
  {
    std::vector<mcp::http::sse::Event> replay;
    for (const StreamEventRecord &record : stream.events)
    {
      if (record.cursor > cursor)
      {
        replay.push_back(record.event);
      }
    }

    return replay;
  }

  auto chooseTargetStream(const std::optional<std::string> &sessionId, const jsonrpc::Message &message) -> StreamState *
  {
    const bool routeToPostStream = isResponseMessage(message);

    StreamState *postFallback = nullptr;
    for (const std::string &streamId : streamOrder)
    {
      auto streamIt = streams.find(streamId);
      if (streamIt == streams.end())
      {
        continue;
      }

      StreamState &stream = streamIt->second;
      if (stream.terminated)
      {
        continue;
      }

      if (stream.sessionId != sessionId)
      {
        continue;
      }

      if (routeToPostStream)
      {
        if (stream.kind == StreamKind::Post)
        {
          return &stream;
        }

        continue;
      }

      if (stream.kind == StreamKind::Get)
      {
        return &stream;
      }

      if (postFallback == nullptr)
      {
        postFallback = &stream;
      }
    }

    if (!routeToPostStream)
    {
      return postFallback;
    }

    return nullptr;
  }

  auto emitPendingMessages(StreamState &stream) -> void
  {
    const std::string key = sessionKey(stream.sessionId);
    auto pending = pendingMessagesBySession.find(key);
    if (pending == pendingMessagesBySession.end())
    {
      return;
    }

    for (const jsonrpc::Message &message : pending->second)
    {
      appendMessageEvent(stream, message);
    }

    pendingMessagesBySession.erase(pending);
  }

  auto validate(const ServerRequest &request, RequestKind requestKind, bool sessionRequiredOverride) const -> RequestValidationResult
  {
    RequestValidationOptions validationOptions;
    validationOptions.requestKind = requestKind;
    validationOptions.sessionRequired = sessionRequiredOverride;
    validationOptions.supportedProtocolVersions = options.http.supportedProtocolVersions;
    validationOptions.originPolicy = options.http.originPolicy;
    validationOptions.sessionResolver = [this](std::string_view sessionId) -> SessionResolution { return sessionResolver(sessionId); };

    return validateServerRequest(request.headers, std::move(validationOptions));
  }

  static auto jsonResponse(std::uint16_t statusCode, std::string body) -> ServerResponse
  {
    ServerResponse response;
    response.statusCode = statusCode;
    response.body = std::move(body);
    setHeader(response.headers, kHeaderContentType, "application/json");
    return response;
  }

  static auto statusResponse(std::uint16_t statusCode) -> ServerResponse
  {
    ServerResponse response;
    response.statusCode = statusCode;
    return response;
  }

  static auto sseResponse(StreamState &stream, std::vector<mcp::http::sse::Event> events, bool terminateStream) -> ServerResponse
  {
    ServerResponse response;
    response.statusCode = kStatusOk;
    setHeader(response.headers, kHeaderContentType, "text/event-stream");
    response.body = mcp::http::sse::encodeEvents(events);

    SseStreamResponse sse;
    sse.streamId = stream.streamId;
    sse.events = std::move(events);
    sse.terminateStream = terminateStream;
    response.sse = std::move(sse);
    return response;
  }

  static auto rejectValidation(const RequestValidationResult &validation) -> ServerResponse
  {
    if (validation.reason.empty())
    {
      return statusResponse(validation.statusCode);
    }

    return jsonResponse(validation.statusCode, makeJsonRpcErrorBody(validation.reason));
  }

  auto handlePost(const ServerRequest &request) -> ServerResponse
  {
    jsonrpc::Message message;
    try
    {
      message = jsonrpc::parseMessage(request.body);
    }
    catch (const std::exception &error)
    {
      return jsonResponse(kStatusBadRequest, makeJsonRpcErrorBody(std::string("Invalid JSON-RPC message: ") + error.what()));
    }

    const RequestKind requestKind = isInitializeRequest(message) ? RequestKind::kInitialize : RequestKind::kOther;
    const RequestValidationResult validation = validate(request, requestKind, options.http.requireSessionId);
    if (!validation.accepted)
    {
      return rejectValidation(validation);
    }

    const jsonrpc::RequestContext context = toRequestContext(validation);

    if (std::holds_alternative<jsonrpc::Notification>(message))
    {
      const bool accepted = !notificationHandler || notificationHandler(context, std::get<jsonrpc::Notification>(message));
      return accepted ? statusResponse(kStatusAccepted) : jsonResponse(kStatusBadRequest, makeJsonRpcErrorBody("Notification was rejected by server policy"));
    }

    if (std::holds_alternative<jsonrpc::SuccessResponse>(message) || std::holds_alternative<jsonrpc::ErrorResponse>(message))
    {
      const jsonrpc::Response typedResponse = std::holds_alternative<jsonrpc::SuccessResponse>(message) ? jsonrpc::Response {std::get<jsonrpc::SuccessResponse>(message)}
                                                                                                        : jsonrpc::Response {std::get<jsonrpc::ErrorResponse>(message)};

      const bool accepted = !responseHandler || responseHandler(context, typedResponse);
      return accepted ? statusResponse(kStatusAccepted) : jsonResponse(kStatusBadRequest, makeJsonRpcErrorBody("Response was rejected by server policy"));
    }

    const jsonrpc::Request &typedRequest = std::get<jsonrpc::Request>(message);

    StreamableRequestResult requestResult;
    if (requestHandler)
    {
      requestResult = requestHandler(context, typedRequest);
    }
    else
    {
      requestResult.response = jsonrpc::Response {jsonrpc::makeErrorResponse(jsonrpc::makeMethodNotFoundError(), typedRequest.id)};
    }

    const bool shouldUseSse = requestResult.useSse || !requestResult.preResponseMessages.empty();
    if (!shouldUseSse)
    {
      if (!requestResult.response.has_value())
      {
        return jsonResponse(kStatusInternalServerError, makeJsonRpcErrorBody("Request handler did not return a response"));
      }

      const jsonrpc::Message responseMessage = std::visit([](const auto &typedResponse) -> jsonrpc::Message { return jsonrpc::Message {typedResponse}; }, *requestResult.response);
      return jsonResponse(kStatusOk, jsonrpc::serializeMessage(responseMessage));
    }

    StreamState &stream = createStream(validation.sessionId, StreamKind::Post);
    for (const jsonrpc::Message &preResponse : requestResult.preResponseMessages)
    {
      appendMessageEvent(stream, preResponse);
    }

    if (requestResult.response.has_value())
    {
      const jsonrpc::Message responseMessage = std::visit([](const auto &typedResponse) -> jsonrpc::Message { return jsonrpc::Message {typedResponse}; }, *requestResult.response);
      appendMessageEvent(stream, responseMessage);
      if (requestResult.terminateSseAfterResponse)
      {
        stream.terminated = true;
      }
    }

    std::vector<mcp::http::sse::Event> outboundEvents = replayFromCursor(stream, 0);
    if (requestResult.closeSseConnection && !stream.terminated && requestResult.retryMilliseconds.has_value())
    {
      outboundEvents.push_back(makeRetryGuidanceEvent(*requestResult.retryMilliseconds));
    }

    return sseResponse(stream, std::move(outboundEvents), stream.terminated ? kTerminateStream : kKeepStreamOpen);
  }

  auto handleGet(const ServerRequest &request) -> ServerResponse
  {
    if (!options.allowGetSse)
    {
      return statusResponse(kStatusMethodNotAllowed);
    }

    const RequestValidationResult validation = validate(request, RequestKind::kOther, options.http.requireSessionId);
    if (!validation.accepted)
    {
      return rejectValidation(validation);
    }

    const auto lastEventId = getHeader(request.headers, kHeaderLastEventId);
    if (!lastEventId.has_value())
    {
      StreamState &stream = createStream(validation.sessionId, StreamKind::Get);
      emitPendingMessages(stream);

      std::vector<mcp::http::sse::Event> outboundEvents = replayFromCursor(stream, 0);
      if (!stream.terminated && options.defaultSseRetryMilliseconds.has_value())
      {
        outboundEvents.push_back(makeRetryGuidanceEvent(*options.defaultSseRetryMilliseconds));
      }

      return sseResponse(stream, std::move(outboundEvents), kKeepStreamOpen);
    }

    const auto eventCursor = mcp::http::sse::parseEventId(*lastEventId);
    if (!eventCursor.has_value())
    {
      return jsonResponse(kStatusBadRequest, makeJsonRpcErrorBody("Invalid Last-Event-ID"));
    }

    auto streamIt = streams.find(eventCursor->streamId);
    if (streamIt == streams.end())
    {
      return statusResponse(kStatusNotFound);
    }

    StreamState &stream = streamIt->second;
    if (stream.sessionId != validation.sessionId)
    {
      return statusResponse(kStatusNotFound);
    }

    std::vector<mcp::http::sse::Event> outboundEvents = replayFromCursor(stream, eventCursor->cursor);
    if (!stream.terminated && options.defaultSseRetryMilliseconds.has_value())
    {
      outboundEvents.push_back(makeRetryGuidanceEvent(*options.defaultSseRetryMilliseconds));
    }

    return sseResponse(stream, std::move(outboundEvents), stream.terminated);
  }

  auto handleDelete(const ServerRequest &request) -> ServerResponse
  {
    if (!options.allowDeleteSession)
    {
      return statusResponse(kStatusMethodNotAllowed);
    }

    const bool sessionRequired = true;
    const RequestValidationResult validation = validate(request, RequestKind::kOther, sessionRequired);
    if (!validation.accepted)
    {
      return rejectValidation(validation);
    }

    if (!validation.sessionId.has_value())
    {
      return jsonResponse(kStatusBadRequest, makeJsonRpcErrorBody("Missing required MCP-Session-Id"));
    }

    const auto session = sessions.find(*validation.sessionId);
    if (session == sessions.end())
    {
      return statusResponse(kStatusNotFound);
    }

    session->second.state = SessionLookupState::kTerminated;

    for (auto &entry : streams)
    {
      if (entry.second.sessionId == validation.sessionId)
      {
        entry.second.terminated = true;
      }
    }

    pendingMessagesBySession.erase(*validation.sessionId);
    return statusResponse(kStatusNoContent);
  }

  auto handleRequest(const ServerRequest &request) -> ServerResponse
  {
    if (request.path != options.http.endpoint.path)
    {
      return statusResponse(kStatusNotFound);
    }

    switch (request.method)
    {
      case ServerRequestMethod::kPost:
        return handlePost(request);
      case ServerRequestMethod::kGet:
        return handleGet(request);
      case ServerRequestMethod::kDelete:
        return handleDelete(request);
    }

    return statusResponse(kStatusMethodNotAllowed);
  }

  auto enqueueServerMessage(const jsonrpc::Message &message, const std::optional<std::string> &sessionId) -> bool
  {
    StreamState *stream = chooseTargetStream(sessionId, message);
    if (stream != nullptr)
    {
      appendMessageEvent(*stream, message);

      if (isResponseMessage(message) && stream->kind == StreamKind::Post)
      {
        stream->terminated = true;
      }

      return true;
    }

    pendingMessagesBySession[sessionKey(sessionId)].push_back(message);
    return true;
  }

  StreamableHttpServerOptions options;
  StreamableRequestHandler requestHandler;
  StreamableNotificationHandler notificationHandler;
  StreamableResponseHandler responseHandler;
  std::uint64_t nextStreamOrdinal = 0;

  std::unordered_map<std::string, SessionResolution> sessions;
  std::unordered_map<std::string, StreamState> streams;
  std::vector<std::string> streamOrder;
  std::unordered_map<std::string, std::vector<jsonrpc::Message>> pendingMessagesBySession;
  mutable std::mutex mutex;
};

StreamableHttpServer::StreamableHttpServer(StreamableHttpServerOptions options)
  : impl_(std::make_unique<Impl>(std::move(options)))
{
}

StreamableHttpServer::~StreamableHttpServer() = default;

StreamableHttpServer::StreamableHttpServer(StreamableHttpServer &&other) noexcept = default;

auto StreamableHttpServer::operator=(StreamableHttpServer &&other) noexcept -> StreamableHttpServer & = default;

auto StreamableHttpServer::setRequestHandler(StreamableRequestHandler handler) -> void
{
  const std::scoped_lock lock(impl_->mutex);
  impl_->requestHandler = std::move(handler);
}

auto StreamableHttpServer::setNotificationHandler(StreamableNotificationHandler handler) -> void
{
  const std::scoped_lock lock(impl_->mutex);
  impl_->notificationHandler = std::move(handler);
}

auto StreamableHttpServer::setResponseHandler(StreamableResponseHandler handler) -> void
{
  const std::scoped_lock lock(impl_->mutex);
  impl_->responseHandler = std::move(handler);
}

auto StreamableHttpServer::upsertSession(std::string sessionId, SessionLookupState state, std::optional<std::string> negotiatedProtocolVersion) -> void
{
  const std::scoped_lock lock(impl_->mutex);
  impl_->upsertSession(std::move(sessionId), state, std::move(negotiatedProtocolVersion));
}

auto StreamableHttpServer::setSessionState(std::string_view sessionId, SessionLookupState state) -> bool
{
  const std::scoped_lock lock(impl_->mutex);
  const auto session = impl_->sessions.find(std::string(sessionId));
  if (session == impl_->sessions.end())
  {
    return false;
  }

  session->second.state = state;
  return true;
}

auto StreamableHttpServer::handleRequest(const ServerRequest &request) -> ServerResponse
{
  const std::scoped_lock lock(impl_->mutex);
  return impl_->handleRequest(request);
}

auto StreamableHttpServer::enqueueServerMessage(const jsonrpc::Message &message, const std::optional<std::string> &sessionId) -> bool
{
  const std::scoped_lock lock(impl_->mutex);
  return impl_->enqueueServerMessage(message, sessionId);
}

}  // namespace http

HttpTransport::HttpTransport(const HttpServerOptions &options)
{
  static_cast<void>(options);
}

HttpTransport::HttpTransport(const HttpClientOptions &options)
{
  static_cast<void>(options);
}

auto HttpTransport::attach(std::weak_ptr<Session> session) -> void
{
  static_cast<void>(session);
}

auto HttpTransport::start() -> void
{
  running_ = true;
}

auto HttpTransport::stop() -> void
{
  running_ = false;
}

auto HttpTransport::isRunning() const noexcept -> bool
{
  return running_;
}

auto HttpTransport::send(jsonrpc::Message message) -> void
{
  static_cast<void>(message);

  if (!running_)
  {
    throw std::runtime_error("HttpTransport must be running before send().");
  }
}

}  // namespace mcp::transport
