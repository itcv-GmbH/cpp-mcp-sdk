#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <mcp/detail/url.hpp>
#include <mcp/http/all.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/transport/http.hpp>

#ifndef MCP_SDK_ENABLE_LEGACY_HTTP_SSE_FALLBACK
#  define MCP_SDK_ENABLE_LEGACY_HTTP_SSE_FALLBACK 0
#endif

namespace mcp::transport::http
{
constexpr std::uint16_t kStatusOk = 200;
constexpr std::uint16_t kStatusMultipleChoices = 300;
constexpr std::uint16_t kStatusAccepted = 202;
constexpr std::uint16_t kStatusBadRequest = 400;
constexpr std::uint16_t kStatusNotFound = 404;
constexpr std::uint16_t kStatusMethodNotAllowed = 405;
constexpr std::uint32_t kDefaultRetryMilliseconds = 1000;
constexpr std::string_view kAcceptJsonAndSse = "application/json, text/event-stream";
constexpr std::string_view kAcceptSseOnly = "text/event-stream";
constexpr std::string_view kJsonContentType = "application/json";
constexpr std::string_view kSseContentType = "text/event-stream";
constexpr std::string_view kLegacyDefaultPostPath = "/rpc";
constexpr std::string_view kLegacyDefaultSsePath = "/events";

enum class ResponseContentType : std::uint8_t
{
  kNone,
  kJson,
  kSse,
  kUnknown,
};

static auto responseContentType(const ServerResponse &response) -> ResponseContentType
{
  const auto contentType = getHeader(response.headers, kHeaderContentType);
  if (!contentType.has_value())
  {
    return response.body.empty() ? ResponseContentType::kNone : ResponseContentType::kUnknown;
  }

  std::string normalized = detail::toLowerAscii(*contentType);
  const std::size_t parametersSeparator = normalized.find(';');
  if (parametersSeparator != std::string::npos)
  {
    normalized = normalized.substr(0, parametersSeparator);
  }

  const std::string_view trimmed = detail::trimAsciiWhitespace(normalized);
  if (trimmed == kJsonContentType)
  {
    return ResponseContentType::kJson;
  }

  if (trimmed == kSseContentType)
  {
    return ResponseContentType::kSse;
  }

  return ResponseContentType::kUnknown;
}

struct ParsedHttpEndpointUrl
{
  std::string scheme;
  std::string host;
  std::string port;
  std::string requestTarget;
};

static auto normalizeRequestTarget(std::string_view rawTarget) -> std::string
{
  std::string normalized(rawTarget);
  const std::size_t fragmentSeparator = normalized.find('#');
  if (fragmentSeparator != std::string::npos)
  {
    normalized.erase(fragmentSeparator);
  }

  if (normalized.empty())
  {
    return "/";
  }

  if (normalized.front() == '?')
  {
    return "/" + normalized;
  }

  if (normalized.front() != '/')
  {
    normalized.insert(normalized.begin(), '/');
  }

  return normalized;
}

static auto parseHttpEndpointUrl(std::string_view endpointUrl) -> std::optional<ParsedHttpEndpointUrl>
{
  const std::optional<mcp::detail::ParsedAbsoluteUrl> parsedAbsolute = mcp::detail::parseAbsoluteUrl(endpointUrl);
  if (!parsedAbsolute.has_value())
  {
    return std::nullopt;
  }

  ParsedHttpEndpointUrl parsed;
  parsed.scheme = parsedAbsolute->scheme;
  if (parsed.scheme != "http" && parsed.scheme != "https")
  {
    return std::nullopt;
  }

  parsed.host = parsedAbsolute->host;
  parsed.port = std::to_string(parsedAbsolute->port);

  std::string requestTarget = parsedAbsolute->path;
  if (parsedAbsolute->query.has_value())
  {
    requestTarget += "?" + *parsedAbsolute->query;
  }

  parsed.requestTarget = normalizeRequestTarget(requestTarget);

  if (parsed.host.empty() || parsed.port.empty())
  {
    return std::nullopt;
  }

  return parsed;
}

static auto sameOrigin(const ParsedHttpEndpointUrl &left, const ParsedHttpEndpointUrl &right) -> bool
{
  return left.scheme == right.scheme && left.host == right.host && left.port == right.port;
}

static auto isLegacyFallbackStatus(std::uint16_t statusCode) -> bool
{
  return statusCode == kStatusBadRequest || statusCode == kStatusNotFound || statusCode == kStatusMethodNotAllowed;
}

static auto endpointPathFromUrl(std::string_view endpointUrl) -> std::string
{
  if (endpointUrl.empty())
  {
    return "/mcp";
  }

  const std::size_t schemeSeparator = endpointUrl.find("://");
  if (schemeSeparator == std::string_view::npos)
  {
    return endpointUrl.front() == '/' ? std::string(endpointUrl) : std::string("/") + std::string(endpointUrl);
  }

  const std::size_t pathStart = endpointUrl.find('/', schemeSeparator + 3);
  if (pathStart == std::string_view::npos)
  {
    return "/mcp";
  }

  return std::string(endpointUrl.substr(pathStart));
}

static auto isInitializeRequest(const jsonrpc::Message &message) -> bool
{
  if (!std::holds_alternative<jsonrpc::Request>(message))
  {
    return false;
  }

  return std::get<jsonrpc::Request>(message).method == "initialize";
}

static auto isRequestMessage(const jsonrpc::Message &message) -> bool
{
  return std::holds_alternative<jsonrpc::Request>(message);
}

static auto isNotificationOrResponse(const jsonrpc::Message &message) -> bool
{
  return std::holds_alternative<jsonrpc::Notification>(message) || std::holds_alternative<jsonrpc::SuccessResponse>(message)
    || std::holds_alternative<jsonrpc::ErrorResponse>(message);
}

static auto asResponse(const jsonrpc::Message &message) -> std::optional<jsonrpc::Response>
{
  if (std::holds_alternative<jsonrpc::SuccessResponse>(message))
  {
    return jsonrpc::Response {std::get<jsonrpc::SuccessResponse>(message)};
  }

  if (std::holds_alternative<jsonrpc::ErrorResponse>(message))
  {
    return jsonrpc::Response {std::get<jsonrpc::ErrorResponse>(message)};
  }

  return std::nullopt;
}

static auto responseMatchesRequestId(const jsonrpc::Response &response, const jsonrpc::RequestId &requestId) -> bool
{
  if (std::holds_alternative<jsonrpc::SuccessResponse>(response))
  {
    return std::get<jsonrpc::SuccessResponse>(response).id == requestId;
  }

  const auto &errorResponse = std::get<jsonrpc::ErrorResponse>(response);
  return errorResponse.id.has_value() && *errorResponse.id == requestId;
}

static auto statusMessage(std::uint16_t statusCode, std::string_view expectation) -> std::string
{
  return "Unexpected HTTP status " + std::to_string(statusCode) + " (expected " + std::string(expectation) + ").";
}

struct ParsedSsePayload
{
  std::vector<jsonrpc::Message> messages;
  std::optional<jsonrpc::Response> matchingResponse;
  std::optional<std::string> lastEventId;
  std::optional<std::uint32_t> retryMilliseconds;
};

struct ParsedLegacySsePayload
{
  std::vector<jsonrpc::Message> messages;
  std::optional<std::string> endpoint;
  std::optional<std::string> lastEventId;
  std::optional<std::uint32_t> retryMilliseconds;
  bool sawEndpointEvent = false;
  bool firstEventWasEndpoint = false;
};

static auto parseSsePayload(std::string_view payload, const std::optional<jsonrpc::RequestId> &awaitedRequestId, std::size_t maxMessageSizeBytes) -> ParsedSsePayload
{
  ParsedSsePayload parsed;
  const std::vector<mcp::http::sse::Event> events = mcp::http::sse::parseEvents(payload);
  parsed.messages.reserve(events.size());

  for (const auto &event : events)
  {
    if (event.id.has_value())
    {
      parsed.lastEventId = event.id;
    }

    if (event.retryMilliseconds.has_value())
    {
      parsed.retryMilliseconds = event.retryMilliseconds;
    }

    if (event.data.empty())
    {
      continue;
    }

    if (event.data.size() > maxMessageSizeBytes)
    {
      throw std::runtime_error("SSE message exceeds configured max message size.");
    }

    const jsonrpc::Message message = jsonrpc::parseMessage(event.data);
    if (awaitedRequestId.has_value())
    {
      const std::optional<jsonrpc::Response> response = asResponse(message);
      if (response.has_value() && responseMatchesRequestId(*response, *awaitedRequestId))
      {
        parsed.matchingResponse = response;
        continue;
      }
    }

    parsed.messages.push_back(message);
  }

  return parsed;
}

static auto parseLegacySsePayload(std::string_view payload, std::size_t maxMessageSizeBytes) -> ParsedLegacySsePayload
{
  ParsedLegacySsePayload parsed;
  const std::vector<mcp::http::sse::Event> events = mcp::http::sse::parseEvents(payload);
  parsed.messages.reserve(events.size());

  for (std::size_t index = 0; index < events.size(); ++index)
  {
    const auto &event = events[index];

    if (event.id.has_value())
    {
      parsed.lastEventId = event.id;
    }

    if (event.retryMilliseconds.has_value())
    {
      parsed.retryMilliseconds = event.retryMilliseconds;
    }

    const std::string_view eventName = event.event.has_value() ? std::string_view(*event.event) : std::string_view("message");
    if (eventName == "endpoint")
    {
      parsed.sawEndpointEvent = true;
      if (index == 0)
      {
        parsed.firstEventWasEndpoint = true;
      }

      if (!parsed.endpoint.has_value())
      {
        parsed.endpoint = std::string(detail::trimAsciiWhitespace(event.data));
      }

      continue;
    }

    if (eventName != "message" || event.data.empty())
    {
      continue;
    }

    if (event.data.size() > maxMessageSizeBytes)
    {
      throw std::runtime_error("Legacy SSE message exceeds configured max message size.");
    }

    parsed.messages.push_back(jsonrpc::parseMessage(event.data));
  }

  return parsed;
}

struct StreamableHttpClient::Impl
{
  struct SseState
  {
    std::optional<std::string> lastEventId;
    std::uint32_t retryMilliseconds = 1000;
    bool active = false;
  };

  struct LegacyState
  {
    std::string postPath;
    std::string ssePath;
    std::optional<std::string> lastEventId;
    std::uint32_t retryMilliseconds = 1000;
    std::vector<jsonrpc::Message> bufferedMessages;
  };

  mutable std::mutex mutex;

  explicit Impl(StreamableHttpClientOptions options, RequestExecutor requestExecutor)
    : options(std::move(options))
    , requestExecutor(std::move(requestExecutor))
  {
    if (!this->requestExecutor)
    {
      throw std::invalid_argument("StreamableHttpClient requires a request executor.");
    }

    if (this->options.endpointUrl.empty())
    {
      throw std::invalid_argument("StreamableHttpClientOptions.endpointUrl must not be empty.");
    }

    if (this->options.defaultRetryMilliseconds == 0)
    {
      this->options.defaultRetryMilliseconds = kDefaultRetryMilliseconds;
    }

    if (this->options.limits.maxRetryDelayMilliseconds > 0)
    {
      this->options.defaultRetryMilliseconds = std::min(this->options.defaultRetryMilliseconds, this->options.limits.maxRetryDelayMilliseconds);
    }

    const bool buildDefaultLegacyFallback = MCP_SDK_ENABLE_LEGACY_HTTP_SSE_FALLBACK != 0;
    legacyFallbackEnabled = this->options.enableLegacyHttpSseFallback.value_or(buildDefaultLegacyFallback);
    if (legacyFallbackEnabled)
    {
      parsedBaseEndpoint = parseHttpEndpointUrl(this->options.endpointUrl);
      if (!parsedBaseEndpoint.has_value())
      {
        throw std::invalid_argument("Legacy HTTP+SSE fallback requires an absolute http(s) StreamableHttpClientOptions.endpointUrl.");
      }
    }
  }

  auto applyAuthorizationHeader(HeaderList &headers) const -> void
  {
    if (options.bearerToken.has_value() && !options.bearerToken->empty())
    {
      setHeader(headers, kHeaderAuthorization, "Bearer " + *options.bearerToken);
    }
  }

  auto applyCommonRequestHeaders(HeaderList &headers, bool isInitializeRequest) const -> void
  {
    options.headerState->replayToRequestHeaders(headers, isInitializeRequest);
    applyAuthorizationHeader(headers);
  }

  auto makePostRequest(const jsonrpc::Message &message) const -> ServerRequest
  {
    ServerRequest request;
    request.method = ServerRequestMethod::kPost;
    request.path = endpointPathFromUrl(options.endpointUrl);
    request.body = jsonrpc::serializeMessage(message);

    setHeader(request.headers, kHeaderAccept, std::string(kAcceptJsonAndSse));
    setHeader(request.headers, kHeaderContentType, std::string(kJsonContentType));
    applyCommonRequestHeaders(request.headers, isInitializeRequest(message));
    return request;
  }

  auto makeLegacyPostRequest(const jsonrpc::Message &message, std::string_view targetPath) const -> ServerRequest
  {
    ServerRequest request;
    request.method = ServerRequestMethod::kPost;
    request.path = std::string(targetPath);
    request.body = jsonrpc::serializeMessage(message);

    setHeader(request.headers, kHeaderAccept, std::string(kJsonContentType));
    setHeader(request.headers, kHeaderContentType, std::string(kJsonContentType));
    applyAuthorizationHeader(request.headers);
    return request;
  }

  auto makeGetRequest(std::optional<std::string> lastEventId) const -> ServerRequest
  {
    ServerRequest request;
    request.method = ServerRequestMethod::kGet;
    request.path = endpointPathFromUrl(options.endpointUrl);

    setHeader(request.headers, kHeaderAccept, std::string(kAcceptSseOnly));
    if (lastEventId.has_value())
    {
      setHeader(request.headers, kHeaderLastEventId, std::move(*lastEventId));
    }

    applyCommonRequestHeaders(request.headers, /*isInitializeRequest=*/false);
    return request;
  }

  auto makeLegacyGetRequest(const LegacyState &state) const -> ServerRequest
  {
    ServerRequest request;
    request.method = ServerRequestMethod::kGet;
    request.path = state.ssePath;

    setHeader(request.headers, kHeaderAccept, std::string(kAcceptSseOnly));
    if (state.lastEventId.has_value())
    {
      setHeader(request.headers, kHeaderLastEventId, *state.lastEventId);
    }

    applyAuthorizationHeader(request.headers);
    return request;
  }

  auto makeDeleteRequest() const -> ServerRequest
  {
    ServerRequest request;
    request.method = ServerRequestMethod::kDelete;
    request.path = endpointPathFromUrl(options.endpointUrl);

    setHeader(request.headers, kHeaderAccept, std::string(kAcceptJsonAndSse));
    applyCommonRequestHeaders(request.headers, /*isInitializeRequest=*/false);
    return request;
  }

  auto resolveLegacyPath(std::string_view configuredValue, std::string_view fallbackPath) const -> std::string
  {
    const std::string_view trimmed = detail::trimAsciiWhitespace(configuredValue);
    if (trimmed.empty())
    {
      return normalizeRequestTarget(fallbackPath);
    }

    if (trimmed.front() == '/' || trimmed.front() == '?')
    {
      return normalizeRequestTarget(trimmed);
    }

    const auto absolute = parseHttpEndpointUrl(trimmed);
    if (!absolute.has_value())
    {
      throw std::runtime_error("Legacy endpoint path must be an absolute path or same-origin absolute URL.");
    }

    if (!parsedBaseEndpoint.has_value() || !sameOrigin(*parsedBaseEndpoint, *absolute))
    {
      throw std::runtime_error("Legacy endpoint path must remain same-origin with the configured endpoint URL.");
    }

    return absolute->requestTarget;
  }

  auto captureSessionFromInitializeResponse(const jsonrpc::Message &requestMessage, const ServerResponse &response) const -> void
  {
    if (!isInitializeRequest(requestMessage) || response.statusCode >= kStatusBadRequest)
    {
      return;
    }

    const auto sessionHeader = getHeader(response.headers, kHeaderMcpSessionId);
    const std::optional<std::string_view> sessionView = sessionHeader.has_value() ? std::optional<std::string_view>(*sessionHeader) : std::nullopt;

    const auto protocolVersionHeader = getHeader(response.headers, kHeaderMcpProtocolVersion);
    const std::string_view protocolVersion = protocolVersionHeader.has_value() ? std::string_view(*protocolVersionHeader) : std::string_view {};

    if (!options.headerState->captureFromInitializeResponse(sessionView, protocolVersion))
    {
      throw std::runtime_error("Initialize response contained an invalid MCP-Session-Id or MCP-Protocol-Version.");
    }
  }

  auto executeRequestSerialized(const ServerRequest &request) const -> ServerResponse
  {
    const std::scoped_lock lock(requestExecutorMutex);
    return requestExecutor(request);
  }

  auto waitForReconnect(std::uint32_t retryMilliseconds) const -> void
  {
    if (options.waitBeforeReconnect)
    {
      options.waitBeforeReconnect(retryMilliseconds);
    }
    else
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(retryMilliseconds));
    }
  }

  auto updateLegacyRetry(LegacyState &state, const std::optional<std::uint32_t> &retryMilliseconds) const -> void
  {
    const std::uint32_t retryCap = options.limits.maxRetryDelayMilliseconds;

    if (retryMilliseconds.has_value() && *retryMilliseconds > 0)
    {
      state.retryMilliseconds = retryCap > 0 ? std::min(*retryMilliseconds, retryCap) : *retryMilliseconds;
      return;
    }

    if (state.retryMilliseconds == 0)
    {
      state.retryMilliseconds = options.defaultRetryMilliseconds;
    }

    if (retryCap > 0)
    {
      state.retryMilliseconds = std::min(state.retryMilliseconds, retryCap);
    }
  }

  auto updateRetry(SseState &state, const std::optional<std::uint32_t> &retryMilliseconds) const -> void
  {
    const std::uint32_t retryCap = options.limits.maxRetryDelayMilliseconds;

    if (retryMilliseconds.has_value() && *retryMilliseconds > 0)
    {
      state.retryMilliseconds = retryCap > 0 ? std::min(*retryMilliseconds, retryCap) : *retryMilliseconds;
      return;
    }

    if (state.retryMilliseconds == 0)
    {
      state.retryMilliseconds = options.defaultRetryMilliseconds;
    }

    if (retryCap > 0)
    {
      state.retryMilliseconds = std::min(state.retryMilliseconds, retryCap);
    }
  }

  auto parseSseResponse(const ServerResponse &response, const std::optional<jsonrpc::RequestId> &awaitedRequestId, SseState &streamState) const -> ParsedSsePayload
  {
    if (response.statusCode != kStatusOk)
    {
      throw std::runtime_error(statusMessage(response.statusCode, "200"));
    }

    const ResponseContentType contentType = responseContentType(response);
    if (contentType != ResponseContentType::kSse)
    {
      throw std::runtime_error("Expected text/event-stream response while processing SSE stream.");
    }

    if (response.body.size() > options.limits.maxMessageSizeBytes)
    {
      throw std::runtime_error("SSE payload exceeds configured max message size.");
    }

    ParsedSsePayload parsed = parseSsePayload(response.body, awaitedRequestId, options.limits.maxMessageSizeBytes);
    if (parsed.lastEventId.has_value())
    {
      streamState.lastEventId = parsed.lastEventId;
    }

    updateRetry(streamState, parsed.retryMilliseconds);
    streamState.active = !response.sse.has_value() || !response.sse->terminateStream;
    return parsed;
  }

  auto parseLegacySseResponse(const ServerResponse &response, LegacyState &state) const -> ParsedLegacySsePayload
  {
    if (response.statusCode != kStatusOk)
    {
      throw std::runtime_error(statusMessage(response.statusCode, "200"));
    }

    const ResponseContentType contentType = responseContentType(response);
    if (contentType != ResponseContentType::kSse)
    {
      throw std::runtime_error("Expected text/event-stream response while using legacy HTTP+SSE fallback transport.");
    }

    if (response.body.size() > options.limits.maxMessageSizeBytes)
    {
      throw std::runtime_error("Legacy SSE payload exceeds configured max message size.");
    }

    ParsedLegacySsePayload parsed = parseLegacySsePayload(response.body, options.limits.maxMessageSizeBytes);
    if (parsed.lastEventId.has_value())
    {
      state.lastEventId = parsed.lastEventId;
    }

    updateLegacyRetry(state, parsed.retryMilliseconds);
    return parsed;
  }

  static auto appendMessagesAndCaptureResponse(StreamableHttpSendResult &result, std::vector<jsonrpc::Message> messages, const std::optional<jsonrpc::RequestId> &awaitedRequestId)
    -> void
  {
    for (auto &message : messages)
    {
      if (awaitedRequestId.has_value() && !result.response.has_value())
      {
        const std::optional<jsonrpc::Response> candidate = asResponse(message);
        if (candidate.has_value() && responseMatchesRequestId(*candidate, *awaitedRequestId))
        {
          result.response = *candidate;
          continue;
        }
      }

      result.messages.push_back(std::move(message));
    }
  }

  auto beginLegacyFallback() -> void
  {
    if (legacyState.has_value())
    {
      return;
    }

    LegacyState state;
    state.postPath = resolveLegacyPath(options.legacyFallbackPostPath, kLegacyDefaultPostPath);
    state.ssePath = resolveLegacyPath(options.legacyFallbackSsePath, kLegacyDefaultSsePath);
    state.retryMilliseconds = options.defaultRetryMilliseconds;

    const ServerResponse eventsResponse = executeRequestSerialized(makeLegacyGetRequest(state));
    ParsedLegacySsePayload parsed = parseLegacySseResponse(eventsResponse, state);
    if (!parsed.firstEventWasEndpoint)
    {
      throw std::runtime_error("Legacy fallback expected initial SSE endpoint event.");
    }

    if (parsed.endpoint.has_value())
    {
      state.postPath = resolveLegacyPath(*parsed.endpoint, state.postPath);
    }

    state.bufferedMessages = std::move(parsed.messages);
    legacyState = std::move(state);
  }

  auto shouldAttemptLegacyFallback(const jsonrpc::Message &message, const ServerResponse &response) const -> bool
  {
    return legacyFallbackEnabled && !legacyState.has_value() && isInitializeRequest(message) && isLegacyFallbackStatus(response.statusCode);
  }

  auto resumeSseStreamUntilResponse(const jsonrpc::RequestId &requestId, SseState &streamState) const -> StreamableHttpSendResult
  {
    StreamableHttpSendResult result;
    const std::uint32_t maxAttempts = options.limits.maxRetryAttempts;
    for (std::uint32_t attempt = 0; attempt < maxAttempts; ++attempt)
    {
      if (!streamState.active)
      {
        throw std::runtime_error("SSE stream terminated before delivering the JSON-RPC response.");
      }

      if (!streamState.lastEventId.has_value())
      {
        throw std::runtime_error("Cannot resume SSE stream without Last-Event-ID.");
      }

      waitForReconnect(streamState.retryMilliseconds);
      const ServerResponse resumed = executeRequestSerialized(makeGetRequest(streamState.lastEventId));
      result.statusCode = resumed.statusCode;

      ParsedSsePayload parsed = parseSseResponse(resumed, requestId, streamState);
      result.messages.insert(result.messages.end(), parsed.messages.begin(), parsed.messages.end());
      if (parsed.matchingResponse.has_value())
      {
        result.response = std::move(parsed.matchingResponse);
        return result;
      }
    }

    throw std::runtime_error("Exceeded configured SSE resume retry attempts without receiving a matching response.");
  }

  auto waitForLegacyResponse(const jsonrpc::RequestId &requestId, StreamableHttpSendResult &result) -> void
  {
    if (!legacyState.has_value())
    {
      throw std::runtime_error("Legacy fallback state was not initialized.");
    }

    LegacyState &state = legacyState.value();
    const std::uint32_t maxAttempts = options.limits.maxRetryAttempts;
    for (std::uint32_t attempt = 0; attempt < maxAttempts; ++attempt)
    {
      if (attempt > 0)
      {
        waitForReconnect(state.retryMilliseconds);
      }

      const ServerResponse eventsResponse = executeRequestSerialized(makeLegacyGetRequest(state));
      result.statusCode = eventsResponse.statusCode;

      ParsedLegacySsePayload parsed = parseLegacySseResponse(eventsResponse, state);
      if (parsed.endpoint.has_value())
      {
        state.postPath = resolveLegacyPath(*parsed.endpoint, state.postPath);
      }

      appendMessagesAndCaptureResponse(result, std::move(parsed.messages), requestId);
      if (result.response.has_value())
      {
        return;
      }
    }

    throw std::runtime_error("Exceeded configured legacy SSE retry attempts without receiving a matching JSON-RPC response.");
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity) - Complex legacy protocol handling
  auto sendLegacy(const jsonrpc::Message &message) -> StreamableHttpSendResult
  {
    if (!legacyState.has_value())
    {
      throw std::runtime_error("Legacy HTTP+SSE fallback is not initialized.");
    }

    LegacyState &legacy = legacyState.value();

    StreamableHttpSendResult result;
    const std::optional<jsonrpc::RequestId> awaitedRequestId = isRequestMessage(message) ? std::optional<jsonrpc::RequestId>(std::get<jsonrpc::Request>(message).id) : std::nullopt;

    appendMessagesAndCaptureResponse(result, std::move(legacy.bufferedMessages), awaitedRequestId);
    legacy.bufferedMessages.clear();

    if (awaitedRequestId.has_value() && result.response.has_value())
    {
      result.statusCode = kStatusOk;
      return result;
    }

    const ServerResponse response = executeRequestSerialized(makeLegacyPostRequest(message, legacy.postPath));
    captureSessionFromInitializeResponse(message, response);
    result.statusCode = response.statusCode;

    if (isNotificationOrResponse(message))
    {
      if (response.statusCode != kStatusAccepted && response.statusCode != kStatusOk)
      {
        throw std::runtime_error(statusMessage(response.statusCode, "202 or 200"));
      }

      if (response.statusCode == kStatusOk && responseContentType(response) == ResponseContentType::kSse)
      {
        ParsedLegacySsePayload parsed = parseLegacySseResponse(response, legacy);
        if (parsed.endpoint.has_value())
        {
          legacy.postPath = resolveLegacyPath(*parsed.endpoint, legacy.postPath);
        }

        appendMessagesAndCaptureResponse(result, std::move(parsed.messages), std::nullopt);
      }

      return result;
    }

    if (!isRequestMessage(message))
    {
      throw std::runtime_error("Unsupported JSON-RPC message kind for legacy HTTP+SSE send.");
    }

    const auto requestId = std::get<jsonrpc::Request>(message).id;
    if (response.statusCode != kStatusAccepted && response.statusCode != kStatusOk)
    {
      throw std::runtime_error(statusMessage(response.statusCode, "202 or 200"));
    }

    if (response.statusCode == kStatusOk)
    {
      const ResponseContentType contentType = responseContentType(response);
      if (contentType == ResponseContentType::kJson)
      {
        if (response.body.size() > options.limits.maxMessageSizeBytes)
        {
          throw std::runtime_error("JSON response exceeds configured max message size.");
        }

        const jsonrpc::Message bodyMessage = jsonrpc::parseMessage(response.body);
        const std::optional<jsonrpc::Response> parsedResponse = asResponse(bodyMessage);
        if (!parsedResponse.has_value())
        {
          throw std::runtime_error("application/json response did not contain a JSON-RPC response object.");
        }

        result.response = parsedResponse;
        return result;
      }

      if (contentType == ResponseContentType::kSse)
      {
        ParsedLegacySsePayload parsed = parseLegacySseResponse(response, legacy);
        if (parsed.endpoint.has_value())
        {
          legacy.postPath = resolveLegacyPath(*parsed.endpoint, legacy.postPath);
        }

        appendMessagesAndCaptureResponse(result, std::move(parsed.messages), requestId);
        if (result.response.has_value())
        {
          return result;
        }
      }
      else if (!response.body.empty())
      {
        throw std::runtime_error("Legacy HTTP+SSE request response used an unsupported content type.");
      }
    }

    waitForLegacyResponse(requestId, result);
    return result;
  }

  auto send(const jsonrpc::Message &message) -> StreamableHttpSendResult
  {
    std::unique_lock lock(mutex);

    if (legacyState.has_value())
    {
      return sendLegacy(message);
    }

    const ServerResponse response = executeRequestSerialized(makePostRequest(message));

    if (shouldAttemptLegacyFallback(message, response))
    {
      beginLegacyFallback();
      return sendLegacy(message);
    }

    // Handle HTTP 404 - session has been terminated by server
    if (response.statusCode == kStatusNotFound)
    {
      options.headerState->clear();
      listenState.reset();
      throw std::runtime_error(statusMessage(response.statusCode, "200"));
    }

    // Handle HTTP 400 - session required but not provided
    if (response.statusCode == kStatusBadRequest)
    {
      const auto errorBody = detail::trimAsciiWhitespace(response.body);
      throw std::runtime_error("HTTP 400: " + std::string(errorBody));
    }

    captureSessionFromInitializeResponse(message, response);

    StreamableHttpSendResult result;
    result.statusCode = response.statusCode;

    if (isNotificationOrResponse(message))
    {
      if (response.statusCode != kStatusAccepted)
      {
        throw std::runtime_error(statusMessage(response.statusCode, "202"));
      }

      return result;
    }

    if (!isRequestMessage(message))
    {
      throw std::runtime_error("Unsupported JSON-RPC message kind for HTTP send.");
    }

    if (response.statusCode != kStatusOk)
    {
      throw std::runtime_error(statusMessage(response.statusCode, "200"));
    }

    const ResponseContentType contentType = responseContentType(response);
    if (contentType == ResponseContentType::kJson)
    {
      if (response.body.size() > options.limits.maxMessageSizeBytes)
      {
        throw std::runtime_error("JSON response exceeds configured max message size.");
      }

      const jsonrpc::Message bodyMessage = jsonrpc::parseMessage(response.body);
      const std::optional<jsonrpc::Response> parsedResponse = asResponse(bodyMessage);
      if (!parsedResponse.has_value())
      {
        throw std::runtime_error("application/json response did not contain a JSON-RPC response object.");
      }

      result.response = parsedResponse;
      return result;
    }

    if (contentType != ResponseContentType::kSse)
    {
      throw std::runtime_error("HTTP response content type was neither application/json nor text/event-stream.");
    }

    SseState streamState;
    streamState.retryMilliseconds = options.defaultRetryMilliseconds;

    const auto requestId = std::get<jsonrpc::Request>(message).id;
    lock.unlock();

    ParsedSsePayload parsed = parseSseResponse(response, requestId, streamState);
    result.messages = parsed.messages;
    if (parsed.matchingResponse.has_value())
    {
      result.response = std::move(parsed.matchingResponse);
      return result;
    }

    StreamableHttpSendResult resumed = resumeSseStreamUntilResponse(requestId, streamState);
    result.messages.insert(result.messages.end(), resumed.messages.begin(), resumed.messages.end());
    result.response = std::move(resumed.response);
    result.statusCode = resumed.statusCode;
    return result;
  }

  auto openListenStream() -> StreamableHttpListenResult
  {
    std::unique_lock lock(mutex);

    if (legacyState.has_value())
    {
      throw std::runtime_error("openListenStream is not supported after legacy HTTP+SSE fallback activation.");
    }

    StreamableHttpListenResult result;

    SseState streamState;
    streamState.retryMilliseconds = options.defaultRetryMilliseconds;
    const ServerResponse response = executeRequestSerialized(makeGetRequest(std::nullopt));
    result.statusCode = response.statusCode;

    // Handle HTTP 404 - session has been terminated by server
    if (response.statusCode == kStatusNotFound)
    {
      options.headerState->clear();
      listenState.reset();
      result.streamOpen = false;
      return result;
    }

    if (response.statusCode == kStatusMethodNotAllowed)
    {
      listenState.reset();
      result.streamOpen = false;
      return result;
    }

    lock.unlock();

    ParsedSsePayload parsed = parseSseResponse(response, std::nullopt, streamState);
    result.messages = std::move(parsed.messages);
    result.streamOpen = streamState.active;

    lock.lock();

    if (streamState.active)
    {
      listenState = std::move(streamState);
    }
    else
    {
      listenState.reset();
    }

    return result;
  }

  auto pollListenStream() -> StreamableHttpListenResult
  {
    std::unique_lock lock(mutex);

    if (legacyState.has_value())
    {
      throw std::runtime_error("pollListenStream is not supported after legacy HTTP+SSE fallback activation.");
    }

    if (!listenState.has_value())
    {
      throw std::runtime_error("No active GET listen stream to poll.");
    }

    SseState &streamState = listenState.value();
    const std::optional<std::string> lastEventId = streamState.lastEventId;
    const std::uint32_t retryMilliseconds = streamState.retryMilliseconds;

    lock.unlock();

    waitForReconnect(retryMilliseconds);
    const ServerResponse response = executeRequestSerialized(makeGetRequest(lastEventId));

    lock.lock();

    StreamableHttpListenResult result;
    result.statusCode = response.statusCode;

    // Handle HTTP 404 - session has been terminated by server, terminate listen loop
    if (response.statusCode == kStatusNotFound)
    {
      options.headerState->clear();
      listenState.reset();
      result.streamOpen = false;
      return result;
    }

    ParsedSsePayload parsed = parseSseResponse(response, std::nullopt, streamState);
    result.messages = std::move(parsed.messages);
    result.streamOpen = streamState.active;

    if (!streamState.active)
    {
      listenState.reset();
    }

    return result;
  }

  [[nodiscard]] auto hasActiveListenStream() const noexcept -> bool { return listenState.has_value(); }

  auto terminateSession() -> bool
  {
    const std::unique_lock lock(mutex);

    if (legacyState.has_value())
    {
      throw std::runtime_error("terminateSession is not supported after legacy HTTP+SSE fallback activation.");
    }

    // Check if we have an active session
    if (!options.headerState->replayOnSubsequentRequests())
    {
      return true;  // No active session to terminate
    }

    const ServerResponse response = executeRequestSerialized(makeDeleteRequest());

    // Handle HTTP 405 Method Not Allowed - server doesn't support DELETE
    if (response.statusCode == kStatusMethodNotAllowed)
    {
      return false;
    }

    // Any other 2xx response indicates successful termination
    if (response.statusCode >= kStatusOk && response.statusCode < kStatusMultipleChoices)
    {
      options.headerState->clear();
      listenState.reset();
      return true;
    }

    // For other error codes, throw an exception
    throw std::runtime_error(statusMessage(response.statusCode, "2xx"));
  }

  StreamableHttpClientOptions options;
  mutable std::mutex requestExecutorMutex;
  RequestExecutor requestExecutor;
  bool legacyFallbackEnabled = false;
  std::optional<ParsedHttpEndpointUrl> parsedBaseEndpoint;
  std::optional<LegacyState> legacyState;
  std::optional<SseState> listenState;
};

StreamableHttpClient::StreamableHttpClient(StreamableHttpClientOptions options, RequestExecutor requestExecutor)
  : impl_(std::make_unique<Impl>(std::move(options), std::move(requestExecutor)))
{
}

StreamableHttpClient::~StreamableHttpClient() = default;

StreamableHttpClient::StreamableHttpClient(StreamableHttpClient &&other) noexcept = default;

auto StreamableHttpClient::operator=(StreamableHttpClient &&other) noexcept -> StreamableHttpClient & = default;

auto StreamableHttpClient::send(const jsonrpc::Message &message) -> StreamableHttpSendResult
{
  return impl_->send(message);
}

auto StreamableHttpClient::openListenStream() -> StreamableHttpListenResult
{
  return impl_->openListenStream();
}

auto StreamableHttpClient::pollListenStream() -> StreamableHttpListenResult
{
  return impl_->pollListenStream();
}

auto StreamableHttpClient::hasActiveListenStream() const noexcept -> bool
{
  return impl_->hasActiveListenStream();
}

auto StreamableHttpClient::terminateSession() -> bool
{
  return impl_->terminateSession();
}

}  // namespace mcp::transport::http
