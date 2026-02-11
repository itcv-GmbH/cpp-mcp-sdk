#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <mcp/http/sse.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/transport/http.hpp>

namespace mcp::transport::http
{
constexpr std::uint16_t kStatusOk = 200;
constexpr std::uint16_t kStatusAccepted = 202;
constexpr std::uint16_t kStatusBadRequest = 400;
constexpr std::uint16_t kStatusMethodNotAllowed = 405;
constexpr std::uint32_t kDefaultRetryMilliseconds = 1000;
constexpr std::uint32_t kMaxResumeAttempts = 64;
constexpr std::string_view kAcceptJsonAndSse = "application/json, text/event-stream";
constexpr std::string_view kAcceptSseOnly = "text/event-stream";
constexpr std::string_view kJsonContentType = "application/json";
constexpr std::string_view kSseContentType = "text/event-stream";

enum class ResponseContentType : std::uint8_t
{
  None,
  Json,
  Sse,
  Unknown,
};

static auto responseContentType(const ServerResponse &response) -> ResponseContentType
{
  const auto contentType = getHeader(response.headers, kHeaderContentType);
  if (!contentType.has_value())
  {
    return response.body.empty() ? ResponseContentType::None : ResponseContentType::Unknown;
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
    return ResponseContentType::Json;
  }

  if (trimmed == kSseContentType)
  {
    return ResponseContentType::Sse;
  }

  return ResponseContentType::Unknown;
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

static auto parseSsePayload(std::string_view payload, const std::optional<jsonrpc::RequestId> &awaitedRequestId) -> ParsedSsePayload
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

struct StreamableHttpClient::Impl
{
  struct SseState
  {
    std::optional<std::string> lastEventId;
    std::uint32_t retryMilliseconds = 1000;
    bool active = false;
  };

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
  }

  auto applyCommonRequestHeaders(HeaderList &headers, bool isInitializeRequest) -> void
  {
    options.sessionState.replayToRequestHeaders(headers);
    options.protocolVersionState.replayToRequestHeaders(headers, isInitializeRequest);

    if (options.bearerToken.has_value() && !options.bearerToken->empty())
    {
      setHeader(headers, "Authorization", "Bearer " + *options.bearerToken);
    }
  }

  auto makePostRequest(const jsonrpc::Message &message) -> ServerRequest
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

  auto makeGetRequest(std::optional<std::string> lastEventId) -> ServerRequest
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

  auto captureSessionFromInitializeResponse(const jsonrpc::Message &requestMessage, const ServerResponse &response) -> void
  {
    if (!isInitializeRequest(requestMessage) || response.statusCode >= kStatusBadRequest)
    {
      return;
    }

    const auto sessionHeader = getHeader(response.headers, kHeaderMcpSessionId);
    const std::optional<std::string_view> sessionView = sessionHeader.has_value() ? std::optional<std::string_view>(*sessionHeader) : std::nullopt;
    if (!options.sessionState.captureFromInitializeResponse(sessionView))
    {
      throw std::runtime_error("Initialize response contained an invalid MCP-Session-Id.");
    }
  }

  auto waitForReconnect(std::uint32_t retryMilliseconds) const -> void
  {
    if (options.waitBeforeReconnect)
    {
      options.waitBeforeReconnect(retryMilliseconds);
    }
  }

  auto updateRetry(SseState &state, const std::optional<std::uint32_t> &retryMilliseconds) const -> void
  {
    if (retryMilliseconds.has_value() && *retryMilliseconds > 0)
    {
      state.retryMilliseconds = *retryMilliseconds;
      return;
    }

    if (state.retryMilliseconds == 0)
    {
      state.retryMilliseconds = options.defaultRetryMilliseconds;
    }
  }

  auto parseSseResponse(const ServerResponse &response, const std::optional<jsonrpc::RequestId> &awaitedRequestId, SseState &streamState) const -> ParsedSsePayload
  {
    if (response.statusCode != kStatusOk)
    {
      throw std::runtime_error(statusMessage(response.statusCode, "200"));
    }

    const ResponseContentType contentType = responseContentType(response);
    if (contentType != ResponseContentType::Sse)
    {
      throw std::runtime_error("Expected text/event-stream response while processing SSE stream.");
    }

    ParsedSsePayload parsed = parseSsePayload(response.body, awaitedRequestId);
    if (parsed.lastEventId.has_value())
    {
      streamState.lastEventId = parsed.lastEventId;
    }

    updateRetry(streamState, parsed.retryMilliseconds);
    streamState.active = !response.sse.has_value() || !response.sse->terminateStream;
    return parsed;
  }

  auto resumeSseStreamUntilResponse(const jsonrpc::RequestId &requestId, SseState &streamState) -> StreamableHttpSendResult
  {
    StreamableHttpSendResult result;
    for (std::uint32_t attempt = 0; attempt < kMaxResumeAttempts; ++attempt)
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
      const ServerResponse resumed = requestExecutor(makeGetRequest(streamState.lastEventId));
      result.statusCode = resumed.statusCode;

      ParsedSsePayload parsed = parseSseResponse(resumed, requestId, streamState);
      result.messages.insert(result.messages.end(), parsed.messages.begin(), parsed.messages.end());
      if (parsed.matchingResponse.has_value())
      {
        result.response = std::move(parsed.matchingResponse);
        return result;
      }
    }

    throw std::runtime_error("Exceeded SSE resume attempts without receiving a matching response.");
  }

  auto send(const jsonrpc::Message &message) -> StreamableHttpSendResult
  {
    const ServerResponse response = requestExecutor(makePostRequest(message));
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
    if (contentType == ResponseContentType::Json)
    {
      const jsonrpc::Message bodyMessage = jsonrpc::parseMessage(response.body);
      const std::optional<jsonrpc::Response> parsedResponse = asResponse(bodyMessage);
      if (!parsedResponse.has_value())
      {
        throw std::runtime_error("application/json response did not contain a JSON-RPC response object.");
      }

      result.response = parsedResponse;
      return result;
    }

    if (contentType != ResponseContentType::Sse)
    {
      throw std::runtime_error("HTTP response content type was neither application/json nor text/event-stream.");
    }

    SseState streamState;
    streamState.retryMilliseconds = options.defaultRetryMilliseconds;

    const auto requestId = std::get<jsonrpc::Request>(message).id;
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
    StreamableHttpListenResult result;

    SseState streamState;
    streamState.retryMilliseconds = options.defaultRetryMilliseconds;
    const ServerResponse response = requestExecutor(makeGetRequest(std::nullopt));
    result.statusCode = response.statusCode;

    if (response.statusCode == kStatusMethodNotAllowed)
    {
      listenState.reset();
      result.streamOpen = false;
      return result;
    }

    ParsedSsePayload parsed = parseSseResponse(response, std::nullopt, streamState);
    result.messages = std::move(parsed.messages);
    result.streamOpen = streamState.active;

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
    if (!listenState.has_value())
    {
      throw std::runtime_error("No active GET listen stream to poll.");
    }

    SseState &streamState = listenState.value();

    waitForReconnect(streamState.retryMilliseconds);
    const ServerResponse response = requestExecutor(makeGetRequest(streamState.lastEventId));

    StreamableHttpListenResult result;
    result.statusCode = response.statusCode;

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

  StreamableHttpClientOptions options;
  RequestExecutor requestExecutor;
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

}  // namespace mcp::transport::http
