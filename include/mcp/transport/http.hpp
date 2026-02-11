#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mcp/http/sse.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/security/origin_policy.hpp>
#include <mcp/transport/transport.hpp>
#include <mcp/version.hpp>

namespace mcp
{
namespace transport
{

namespace http
{

inline constexpr std::string_view kHeaderAccept = "Accept";
inline constexpr std::string_view kHeaderContentType = "Content-Type";
inline constexpr std::string_view kHeaderOrigin = "Origin";
inline constexpr std::string_view kHeaderLastEventId = "Last-Event-ID";
inline constexpr std::string_view kHeaderMcpSessionId = "MCP-Session-Id";
inline constexpr std::string_view kHeaderMcpProtocolVersion = "MCP-Protocol-Version";

struct Header
{
  std::string name;
  std::string value;
};

using HeaderList = std::vector<Header>;

namespace detail
{

inline auto toLowerAscii(std::string_view text) -> std::string
{
  std::string normalized;
  normalized.reserve(text.size());

  for (const char character : text)
  {
    normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }

  return normalized;
}

inline auto equalsIgnoreCase(std::string_view left, std::string_view right) -> bool
{
  if (left.size() != right.size())
  {
    return false;
  }

  for (std::size_t index = 0; index < left.size(); ++index)
  {
    if (std::tolower(static_cast<unsigned char>(left[index])) != std::tolower(static_cast<unsigned char>(right[index])))
    {
      return false;
    }
  }

  return true;
}

inline auto trimAsciiWhitespace(std::string_view value) -> std::string_view
{
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0)
  {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
  {
    --end;
  }

  return value.substr(begin, end - begin);
}

}  // namespace detail

inline auto setHeader(HeaderList &headers, std::string_view name, std::string value) -> void
{
  const auto existing = std::find_if(headers.begin(), headers.end(), [name](const Header &header) -> bool { return detail::equalsIgnoreCase(header.name, name); });

  if (existing != headers.end())
  {
    existing->value = std::move(value);
    return;
  }

  headers.push_back(Header {std::string(name), std::move(value)});
}

inline auto getHeader(const HeaderList &headers, std::string_view name) -> std::optional<std::string>
{
  const auto existing = std::find_if(headers.begin(), headers.end(), [name](const Header &header) -> bool { return detail::equalsIgnoreCase(header.name, name); });

  if (existing == headers.end())
  {
    return std::nullopt;
  }

  return existing->value;
}

inline auto isValidSessionId(std::string_view sessionId) -> bool
{
  if (sessionId.empty())
  {
    return false;
  }

  return std::all_of(sessionId.begin(),
                     sessionId.end(),
                     [](char character) -> bool
                     {
                       const auto byte = static_cast<unsigned char>(character);
                       return byte >= 0x21U && byte <= 0x7EU;
                     });
}

inline auto isValidProtocolVersion(std::string_view version) -> bool
{
  if (version.size() != 10 || version[4] != '-' || version[7] != '-')
  {
    return false;
  }

  for (std::size_t index = 0; index < version.size(); ++index)
  {
    if (index == 4 || index == 7)
    {
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(version[index])) == 0)
    {
      return false;
    }
  }

  return true;
}

inline auto isSupportedProtocolVersion(std::string_view version, const std::vector<std::string> &supportedVersions) -> bool
{
  if (supportedVersions.empty())
  {
    return true;
  }

  return std::any_of(supportedVersions.begin(), supportedVersions.end(), [version](const std::string &supportedVersion) -> bool { return supportedVersion == version; });
}

class SessionHeaderState
{
public:
  auto captureFromInitializeResponse(std::optional<std::string_view> sessionHeader) -> bool
  {
    if (!sessionHeader.has_value())
    {
      clear();
      return true;
    }

    const std::string_view normalizedSessionId = detail::trimAsciiWhitespace(*sessionHeader);
    if (!isValidSessionId(normalizedSessionId))
    {
      clear();
      return false;
    }

    sessionId_ = std::string(normalizedSessionId);
    replayOnSubsequentRequests_ = true;
    return true;
  }

  auto clear() noexcept -> void
  {
    sessionId_.reset();
    replayOnSubsequentRequests_ = false;
  }

  [[nodiscard]] auto sessionId() const noexcept -> const std::optional<std::string> & { return sessionId_; }

  [[nodiscard]] auto replayOnSubsequentRequests() const noexcept -> bool { return replayOnSubsequentRequests_ && sessionId_.has_value(); }

  auto replayToRequestHeaders(HeaderList &headers) const -> void
  {
    if (!replayOnSubsequentRequests())
    {
      return;
    }

    setHeader(headers, kHeaderMcpSessionId, *sessionId_);
  }

private:
  std::optional<std::string> sessionId_;
  bool replayOnSubsequentRequests_ = false;
};

class ProtocolVersionHeaderState
{
public:
  auto setNegotiatedProtocolVersion(std::string protocolVersion) -> bool
  {
    const std::string_view normalizedVersion = detail::trimAsciiWhitespace(protocolVersion);
    if (!isValidProtocolVersion(normalizedVersion))
    {
      clear();
      return false;
    }

    negotiatedProtocolVersion_ = std::string(normalizedVersion);
    return true;
  }

  auto clear() noexcept -> void { negotiatedProtocolVersion_.reset(); }

  [[nodiscard]] auto negotiatedProtocolVersion() const noexcept -> const std::optional<std::string> & { return negotiatedProtocolVersion_; }

  auto replayToRequestHeaders(HeaderList &headers, bool isInitializeRequest = false) const -> void
  {
    if (!negotiatedProtocolVersion_.has_value() || isInitializeRequest)
    {
      return;
    }

    setHeader(headers, kHeaderMcpProtocolVersion, *negotiatedProtocolVersion_);
  }

private:
  std::optional<std::string> negotiatedProtocolVersion_;
};

enum class RequestKind
{
  kInitialize,
  kOther,
};

enum class SessionLookupState
{
  kUnknown,
  kActive,
  kExpired,
  kTerminated,
};

struct SessionResolution
{
  SessionLookupState state = SessionLookupState::kUnknown;
  std::optional<std::string> negotiatedProtocolVersion;
};

using SessionResolver = std::function<SessionResolution(std::string_view sessionId)>;

struct RequestValidationOptions
{
  RequestKind requestKind = RequestKind::kOther;
  bool sessionRequired = false;
  std::vector<std::string> supportedProtocolVersions = {
    std::string(kLatestProtocolVersion),
    std::string(kLegacyProtocolVersion),
    std::string(kFallbackProtocolVersion),
  };
  std::optional<std::string> inferredProtocolVersion;
  security::OriginPolicy originPolicy;
  SessionResolver sessionResolver;
};

struct RequestValidationResult
{
  bool accepted = true;
  std::uint16_t statusCode = 200;
  std::string reason;
  std::optional<std::string> sessionId;
  std::string effectiveProtocolVersion;
};

inline auto rejectRequest(std::uint16_t statusCode, std::string reason) -> RequestValidationResult
{
  RequestValidationResult result;
  result.accepted = false;
  result.statusCode = statusCode;
  result.reason = std::move(reason);
  return result;
}

inline auto validateServerRequest(const HeaderList &headers, RequestValidationOptions options = {}) -> RequestValidationResult
{
  if (options.originPolicy.validateOrigin)
  {
    const auto origin = getHeader(headers, kHeaderOrigin);
    if (!origin.has_value() && !options.originPolicy.allowRequestsWithoutOrigin)
    {
      return rejectRequest(403, "Origin is required");
    }

    if (origin.has_value() && !security::isOriginAllowed(*origin, options.originPolicy))
    {
      return rejectRequest(403, "Origin is not allowed");
    }
  }

  RequestValidationResult accepted;

  const auto sessionIdHeader = getHeader(headers, kHeaderMcpSessionId);
  if (sessionIdHeader.has_value())
  {
    const std::string_view normalizedSessionId = detail::trimAsciiWhitespace(*sessionIdHeader);
    if (!isValidSessionId(normalizedSessionId))
    {
      return rejectRequest(400, "Invalid MCP-Session-Id");
    }

    accepted.sessionId = std::string(normalizedSessionId);
  }
  else if (options.sessionRequired && options.requestKind != RequestKind::kInitialize)
  {
    return rejectRequest(400, "Missing required MCP-Session-Id");
  }

  if (accepted.sessionId.has_value() && options.sessionResolver)
  {
    const SessionResolution resolution = options.sessionResolver(*accepted.sessionId);
    if (resolution.state == SessionLookupState::kExpired || resolution.state == SessionLookupState::kTerminated || resolution.state == SessionLookupState::kUnknown)
    {
      return rejectRequest(404, "Session is not active");
    }

    if (!options.inferredProtocolVersion.has_value() && resolution.negotiatedProtocolVersion.has_value())
    {
      options.inferredProtocolVersion = resolution.negotiatedProtocolVersion;
    }
  }

  const auto headerProtocolVersion = getHeader(headers, kHeaderMcpProtocolVersion);
  if (headerProtocolVersion.has_value())
  {
    const std::string_view normalizedVersion = detail::trimAsciiWhitespace(*headerProtocolVersion);
    if (!isValidProtocolVersion(normalizedVersion))
    {
      return rejectRequest(400, "Invalid MCP-Protocol-Version");
    }

    if (!isSupportedProtocolVersion(normalizedVersion, options.supportedProtocolVersions))
    {
      return rejectRequest(400, "Unsupported MCP-Protocol-Version");
    }

    accepted.effectiveProtocolVersion = std::string(normalizedVersion);
    return accepted;
  }

  if (options.inferredProtocolVersion.has_value())
  {
    const std::string_view normalizedVersion = detail::trimAsciiWhitespace(*options.inferredProtocolVersion);
    if (!isValidProtocolVersion(normalizedVersion) || !isSupportedProtocolVersion(normalizedVersion, options.supportedProtocolVersions))
    {
      return rejectRequest(400, "Inferred protocol version is invalid or unsupported");
    }

    accepted.effectiveProtocolVersion = std::string(normalizedVersion);
    return accepted;
  }

  accepted.effectiveProtocolVersion = std::string(kFallbackProtocolVersion);
  return accepted;
}

}  // namespace http

struct HttpEndpointConfig
{
  std::string path = "/mcp";
  std::string bindAddress = "127.0.0.1";
  std::uint16_t port = 0;
  bool bindLocalhostOnly = true;
};

struct HttpServerOptions
{
  HttpEndpointConfig endpoint;
  security::OriginPolicy originPolicy;
  bool requireSessionId = false;
  std::vector<std::string> supportedProtocolVersions = {
    std::string(kLatestProtocolVersion),
    std::string(kLegacyProtocolVersion),
    std::string(kFallbackProtocolVersion),
  };
};

namespace http
{

enum class ServerRequestMethod
{
  kGet,
  kPost,
  kDelete,
};

struct ServerRequest
{
  ServerRequestMethod method = ServerRequestMethod::kPost;
  std::string path = "/mcp";
  HeaderList headers;
  std::string body;
};

struct SseStreamResponse
{
  std::string streamId;
  std::vector<mcp::http::sse::Event> events;
  bool terminateStream = false;
};

struct ServerResponse
{
  std::uint16_t statusCode = 200;
  HeaderList headers;
  std::string body;
  std::optional<SseStreamResponse> sse;
};

struct StreamableRequestResult
{
  std::vector<jsonrpc::Message> preResponseMessages;
  std::optional<jsonrpc::Response> response;
  bool useSse = false;
  bool terminateSseAfterResponse = true;
  bool closeSseConnection = false;
  std::optional<std::uint32_t> retryMilliseconds;
};

using StreamableRequestHandler = std::function<StreamableRequestResult(const jsonrpc::RequestContext &, const jsonrpc::Request &)>;
using StreamableNotificationHandler = std::function<bool(const jsonrpc::RequestContext &, const jsonrpc::Notification &)>;
using StreamableResponseHandler = std::function<bool(const jsonrpc::RequestContext &, const jsonrpc::Response &)>;

struct StreamableHttpServerOptions
{
  HttpServerOptions http;
  bool allowGetSse = true;
  bool allowDeleteSession = true;
  std::optional<std::uint32_t> defaultSseRetryMilliseconds;
};

class StreamableHttpServer
{
public:
  explicit StreamableHttpServer(StreamableHttpServerOptions options = {});
  ~StreamableHttpServer();

  StreamableHttpServer(const StreamableHttpServer &) = delete;
  auto operator=(const StreamableHttpServer &) -> StreamableHttpServer & = delete;
  StreamableHttpServer(StreamableHttpServer &&other) noexcept;
  auto operator=(StreamableHttpServer &&other) noexcept -> StreamableHttpServer &;

  auto setRequestHandler(StreamableRequestHandler handler) -> void;
  auto setNotificationHandler(StreamableNotificationHandler handler) -> void;
  auto setResponseHandler(StreamableResponseHandler handler) -> void;

  auto upsertSession(std::string sessionId,
                     SessionLookupState state = SessionLookupState::kActive,
                     std::optional<std::string> negotiatedProtocolVersion = std::string(kLatestProtocolVersion)) -> void;
  auto setSessionState(std::string_view sessionId, SessionLookupState state) -> bool;

  [[nodiscard]] auto handleRequest(const ServerRequest &request) -> ServerResponse;
  [[nodiscard]] auto enqueueServerMessage(const jsonrpc::Message &message, const std::optional<std::string> &sessionId = std::nullopt) -> bool;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

struct StreamableHttpSendResult
{
  std::uint16_t statusCode = 0;
  std::vector<jsonrpc::Message> messages;
  std::optional<jsonrpc::Response> response;
};

struct StreamableHttpListenResult
{
  std::uint16_t statusCode = 0;
  std::vector<jsonrpc::Message> messages;
  bool streamOpen = false;
};

struct StreamableHttpClientOptions
{
  std::string endpointUrl;
  std::optional<std::string> bearerToken;
  SessionHeaderState sessionState;
  ProtocolVersionHeaderState protocolVersionState;
  std::uint32_t defaultRetryMilliseconds = 1000;
  std::function<void(std::uint32_t)> waitBeforeReconnect;
};

class StreamableHttpClient
{
public:
  using RequestExecutor = std::function<ServerResponse(const ServerRequest &)>;

  explicit StreamableHttpClient(StreamableHttpClientOptions options, RequestExecutor requestExecutor);
  ~StreamableHttpClient();

  StreamableHttpClient(const StreamableHttpClient &) = delete;
  auto operator=(const StreamableHttpClient &) -> StreamableHttpClient & = delete;
  StreamableHttpClient(StreamableHttpClient &&other) noexcept;
  auto operator=(StreamableHttpClient &&other) noexcept -> StreamableHttpClient &;

  auto send(const jsonrpc::Message &message) -> StreamableHttpSendResult;
  auto openListenStream() -> StreamableHttpListenResult;
  auto pollListenStream() -> StreamableHttpListenResult;
  [[nodiscard]] auto hasActiveListenStream() const noexcept -> bool;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace http

struct HttpClientOptions
{
  std::string endpointUrl;
  std::optional<std::string> bearerToken;
  http::SessionHeaderState sessionState;
  http::ProtocolVersionHeaderState protocolVersionState;
};

class HttpTransport final : public Transport
{
public:
  explicit HttpTransport(const HttpServerOptions &options);
  explicit HttpTransport(const HttpClientOptions &options);

  auto attach(std::weak_ptr<Session> session) -> void override;
  auto start() -> void override;
  auto stop() -> void override;
  auto isRunning() const noexcept -> bool override;
  auto send(jsonrpc::Message message) -> void override;

private:
  bool running_ = false;
};

}  // namespace transport
}  // namespace mcp
