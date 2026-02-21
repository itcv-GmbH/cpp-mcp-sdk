#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mcp/auth/oauth_server.hpp>
#include <mcp/detail/ascii.hpp>
#include <mcp/http/sse.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/security/limits.hpp>
#include <mcp/security/origin_policy.hpp>
#include <mcp/transport/transport.hpp>
#include <mcp/version.hpp>

namespace mcp::transport
{
/**
 * @brief HTTP transport implementation for MCP SDK.
 *
 * @section Exceptions
 *
 * @subsection StreamableHttpServer
 * - Constructor: Does not throw
 * - Destructor: Standard destructor behavior
 * - Move operations: noexcept
 * - setRequestHandler(), setNotificationHandler(), setResponseHandler(): Do not throw
 * - upsertSession(), setSessionState(): Do not throw
 * - handleRequest(): Returns ServerResponse; errors encoded in response
 * - enqueueServerMessage(): Returns bool success
 *
 * @subsection StreamableHttpClient
 * - Constructor: May throw std::invalid_argument for invalid options
 * - Destructor: Standard destructor behavior
 * - Move operations: noexcept
 * - send(): Throws std::runtime_error on HTTP error or serialization failure
 * - openListenStream(): Throws std::runtime_error on connection failure
 * - pollListenStream(): Returns StreamableHttpListenResult
 * - hasActiveListenStream() noexcept
 * - terminateSession(): Returns bool
 *
 * @subsection HttpServerRuntime
 * - Constructor: Does not throw
 * - Destructor: Standard destructor behavior
 * - Move operations: noexcept
 * - setRequestHandler(): Does not throw
 * - start(): Throws std::runtime_error on server startup failure
 * - stop() noexcept
 * - isRunning() const noexcept
 * - localPort() const noexcept
 *
 * @subsection HttpClientRuntime
 * - Constructor: Does not throw
 * - Destructor: Standard destructor behavior
 * - Move operations: noexcept
 * - execute() const: Throws std::runtime_error on HTTP request failure
 *
 * @subsection Header Operations
 * - setHeader(), getHeader(): Inline helpers; may throw on memory allocation
 * - isValidSessionId(), isValidProtocolVersion(): noexcept
 * - isSupportedProtocolVersion(): noexcept
 *
 * @subsection State Classes
 * - SessionHeaderState, ProtocolVersionHeaderState, SharedHeaderState:
 *   - clear() noexcept
 *   - Accessor methods: noexcept or simple returns
 *   - captureFromInitializeResponse(), setNegotiatedProtocolVersion(): Return bool success
 *
 * @subsection Request Validation
 * - rejectRequest(): Returns RequestValidationResult
 * - validateServerRequest(): Returns RequestValidationResult; errors encoded in result
 */

/**
 * @brief Thread Safety
 *
 * This header defines several HTTP transport types with varying thread-safety classifications:
 *
 * @par HttpServerRuntime - Thread-safe
 * - Provides internal synchronization for all public methods
 * - Thread-safe methods: setRequestHandler(), start(), stop(), isRunning(), localPort()
 * - start() is idempotent
 * - stop() is idempotent and noexcept
 * - Handler callbacks are invoked on HTTP server internal I/O threads
 *
 * @par HttpClientRuntime - Thread-compatible
 * - Designed for single-threaded or externally synchronized use
 * - execute() must not be called concurrently
 * - External synchronization required for concurrent use
 *
 * @par http::StreamableHttpServer - Thread-safe
 * - Provides internal synchronization for session management and request handling
 * - Thread-safe methods: setRequestHandler(), setNotificationHandler(), setResponseHandler(),
 *   upsertSession(), setSessionState(), handleRequest(), enqueueServerMessage()
 * - handleRequest() is thread-safe and may be called concurrently from multiple HTTP worker threads
 * - Handler callbacks are invoked on HTTP server internal I/O threads
 *
 * @par http::StreamableHttpClient - Thread-compatible
 * - Designed for single-threaded or externally synchronized use
 * - Mutating methods (send(), openListenStream(), pollListenStream(), terminateSession())
 *   must not be called concurrently
 * - hasActiveListenStream() is thread-safe for queries
 * - External synchronization required for concurrent use
 *
 * @par http::SharedHeaderState - Thread-safe
 * - Provides internal synchronization via mutex_
 * - All accessor and mutator methods are thread-safe
 */

namespace http
{

inline constexpr std::string_view kHeaderAccept = "Accept";
inline constexpr std::string_view kHeaderContentType = "Content-Type";
inline constexpr std::string_view kHeaderOrigin = "Origin";
inline constexpr std::string_view kHeaderLastEventId = "Last-Event-ID";
inline constexpr std::string_view kHeaderMcpSessionId = "MCP-Session-Id";
inline constexpr std::string_view kHeaderMcpProtocolVersion = "MCP-Protocol-Version";
inline constexpr std::string_view kHeaderAuthorization = "Authorization";
inline constexpr std::string_view kHeaderWwwAuthenticate = "WWW-Authenticate";

struct Header
{
  std::string name;
  std::string value;
};

using HeaderList = std::vector<Header>;

enum class TlsClientAuthenticationMode : std::uint8_t
{
  kNone,
  kOptional,
  kRequired,
};

struct ServerTlsConfiguration
{
  std::string certificateChainFile;
  std::string privateKeyFile;
  std::optional<std::string> privateKeyPassphrase;
  TlsClientAuthenticationMode clientAuthenticationMode = TlsClientAuthenticationMode::kNone;
  std::optional<std::string> clientCaCertificateFile;
  std::optional<std::string> clientCaCertificatePath;
};

struct ClientTlsConfiguration
{
  bool verifyPeer = true;
  std::optional<std::string> caCertificateFile;
  std::optional<std::string> caCertificatePath;
  std::optional<std::string> serverNameIndication;
};

namespace detail
{

inline constexpr unsigned char kVisibleAsciiFirst = 0x21U;
inline constexpr unsigned char kVisibleAsciiLast = 0x7EU;
inline constexpr std::size_t kProtocolVersionLength = 10U;
inline constexpr std::size_t kProtocolVersionFirstDash = 4U;
inline constexpr std::size_t kProtocolVersionSecondDash = 7U;
inline constexpr std::uint16_t kHttpStatusOk = 200;
inline constexpr std::uint16_t kHttpStatusBadRequest = 400;
inline constexpr std::uint16_t kHttpStatusForbidden = 403;
inline constexpr std::uint16_t kHttpStatusNotFound = 404;
inline constexpr std::uint16_t kHttpStatusMethodNotAllowed = 405;
inline constexpr std::uint32_t kDefaultRetryMilliseconds = 1000U;

using ::mcp::detail::equalsIgnoreCaseAscii;
using ::mcp::detail::toLowerAscii;
using ::mcp::detail::trimAsciiWhitespace;

}  // namespace detail

inline auto setHeader(HeaderList &headers, std::string_view name, std::string value) -> void
{
  const auto existing = std::find_if(headers.begin(), headers.end(), [name](const Header &header) -> bool { return detail::equalsIgnoreCaseAscii(header.name, name); });

  if (existing != headers.end())
  {
    existing->value = std::move(value);
    return;
  }

  headers.push_back(Header {std::string(name), std::move(value)});
}

inline auto getHeader(const HeaderList &headers, std::string_view name) -> std::optional<std::string>
{
  const auto existing = std::find_if(headers.begin(), headers.end(), [name](const Header &header) -> bool { return detail::equalsIgnoreCaseAscii(header.name, name); });

  if (existing == headers.end())
  {
    return std::nullopt;
  }

  return existing->value;
}

inline auto isValidSessionId(std::string_view sessionId) noexcept -> bool
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
                       return byte >= detail::kVisibleAsciiFirst && byte <= detail::kVisibleAsciiLast;
                     });
}

inline auto isValidProtocolVersion(std::string_view version) noexcept -> bool
{
  if (version.size() != detail::kProtocolVersionLength || version[detail::kProtocolVersionFirstDash] != '-' || version[detail::kProtocolVersionSecondDash] != '-')
  {
    return false;
  }

  for (std::size_t index = 0; index < version.size(); ++index)
  {
    if (index == detail::kProtocolVersionFirstDash || index == detail::kProtocolVersionSecondDash)
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

inline auto isSupportedProtocolVersion(std::string_view version, const std::vector<std::string> &supportedVersions) noexcept -> bool
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

    if (sessionId_.has_value())
    {
      setHeader(headers, kHeaderMcpSessionId, *sessionId_);
    }
  }

private:
  std::optional<std::string> sessionId_;
  bool replayOnSubsequentRequests_ = false;
};

class ProtocolVersionHeaderState
{
public:
  auto setNegotiatedProtocolVersion(std::string_view protocolVersion) -> bool
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

class SharedHeaderState final
{
public:
  auto captureFromInitializeResponse(std::optional<std::string_view> sessionHeader, std::string_view protocolVersion) -> bool
  {
    const std::scoped_lock lock(mutex_);

    if (!sessionState_.captureFromInitializeResponse(sessionHeader))
    {
      return false;
    }

    if (!protocolVersion.empty())
    {
      protocolVersionState_.setNegotiatedProtocolVersion(protocolVersion);
    }

    return true;
  }

  auto clear() noexcept -> void
  {
    const std::scoped_lock lock(mutex_);
    sessionState_.clear();
    protocolVersionState_.clear();
  }

  [[nodiscard]] auto sessionId() const -> std::optional<std::string>
  {
    const std::scoped_lock lock(mutex_);
    return sessionState_.sessionId();
  }

  [[nodiscard]] auto replayOnSubsequentRequests() const noexcept -> bool
  {
    const std::scoped_lock lock(mutex_);
    return sessionState_.replayOnSubsequentRequests();
  }

  [[nodiscard]] auto negotiatedProtocolVersion() const -> std::optional<std::string>
  {
    const std::scoped_lock lock(mutex_);
    return protocolVersionState_.negotiatedProtocolVersion();
  }

  auto replayToRequestHeaders(HeaderList &headers, bool isInitializeRequest = false) const -> void
  {
    const std::scoped_lock lock(mutex_);
    sessionState_.replayToRequestHeaders(headers);
    protocolVersionState_.replayToRequestHeaders(headers, isInitializeRequest);
  }

private:
  mutable std::mutex mutex_;
  SessionHeaderState sessionState_;
  ProtocolVersionHeaderState protocolVersionState_;
};

enum class RequestKind : std::uint8_t
{
  kInitialize,
  kOther,
};

enum class SessionLookupState : std::uint8_t
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
  std::uint16_t statusCode = detail::kHttpStatusOk;
  std::string reason;
  std::optional<std::string> sessionId;
  std::string effectiveProtocolVersion;
  std::optional<auth::OAuthAuthorizationContext> authorizationContext;
};

inline auto rejectRequest(std::uint16_t statusCode, std::string reason) -> RequestValidationResult
{
  RequestValidationResult result;
  result.accepted = false;
  result.statusCode = statusCode;
  result.reason = std::move(reason);
  return result;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
inline auto validateServerRequest(const HeaderList &headers, RequestValidationOptions options = {}) -> RequestValidationResult
{
  if (options.originPolicy.validateOrigin)
  {
    const auto origin = getHeader(headers, kHeaderOrigin);
    if (!origin.has_value() && !options.originPolicy.allowRequestsWithoutOrigin)
    {
      return rejectRequest(detail::kHttpStatusForbidden, "Origin is required");
    }

    if (origin.has_value() && !security::isOriginAllowed(*origin, options.originPolicy))
    {
      return rejectRequest(detail::kHttpStatusForbidden, "Origin is not allowed");
    }
  }

  RequestValidationResult accepted;

  const auto sessionIdHeader = getHeader(headers, kHeaderMcpSessionId);
  if (sessionIdHeader.has_value())
  {
    const std::string_view normalizedSessionId = detail::trimAsciiWhitespace(*sessionIdHeader);
    if (!isValidSessionId(normalizedSessionId))
    {
      return rejectRequest(detail::kHttpStatusBadRequest, "Invalid MCP-Session-Id");
    }

    accepted.sessionId = std::string(normalizedSessionId);
  }
  else if (options.sessionRequired && options.requestKind != RequestKind::kInitialize)
  {
    return rejectRequest(detail::kHttpStatusBadRequest, "Missing required MCP-Session-Id");
  }

  if (accepted.sessionId.has_value() && options.sessionResolver)
  {
    const SessionResolution resolution = options.sessionResolver(*accepted.sessionId);
    if (resolution.state == SessionLookupState::kExpired || resolution.state == SessionLookupState::kTerminated || resolution.state == SessionLookupState::kUnknown)
    {
      return rejectRequest(detail::kHttpStatusNotFound, "Session is not active");
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
      return rejectRequest(detail::kHttpStatusBadRequest, "Invalid MCP-Protocol-Version");
    }

    if (!isSupportedProtocolVersion(normalizedVersion, options.supportedProtocolVersions))
    {
      return rejectRequest(detail::kHttpStatusBadRequest, "Unsupported MCP-Protocol-Version");
    }

    accepted.effectiveProtocolVersion = std::string(normalizedVersion);
    return accepted;
  }

  if (options.inferredProtocolVersion.has_value())
  {
    const std::string_view normalizedVersion = detail::trimAsciiWhitespace(*options.inferredProtocolVersion);
    if (!isValidProtocolVersion(normalizedVersion) || !isSupportedProtocolVersion(normalizedVersion, options.supportedProtocolVersions))
    {
      return rejectRequest(detail::kHttpStatusBadRequest, "Inferred protocol version is invalid or unsupported");
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
  std::optional<http::ServerTlsConfiguration> tls;
  security::OriginPolicy originPolicy;
  security::RuntimeLimits limits;
  std::optional<auth::OAuthServerAuthorizationOptions> authorization;
  bool requireSessionId = false;
  std::vector<std::string> supportedProtocolVersions = {
    std::string(kLatestProtocolVersion),
    std::string(kLegacyProtocolVersion),
    std::string(kFallbackProtocolVersion),
  };
};

namespace http
{

enum class ServerRequestMethod : std::uint8_t
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
  std::uint16_t statusCode = detail::kHttpStatusOk;
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
  std::optional<bool> enableLegacyHttpSseCompatibility;
  std::string legacyPostEndpointPath = "/rpc";
  std::string legacySseEndpointPath = "/events";
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
  ClientTlsConfiguration tls;
  security::RuntimeLimits limits;
  std::shared_ptr<SharedHeaderState> headerState = std::make_shared<SharedHeaderState>();
  std::optional<bool> enableLegacyHttpSseFallback;
  std::string legacyFallbackPostPath = "/rpc";
  std::string legacyFallbackSsePath = "/events";
  std::uint32_t defaultRetryMilliseconds = detail::kDefaultRetryMilliseconds;
  std::function<void(std::uint32_t)> waitBeforeReconnect;

  // Enable GET SSE listen behavior for server-initiated messages.
  // When enabled, the client will use HTTP GET requests to listen for server messages
  // via SSE (Server-Sent Events), as specified in MCP 2025-11-25 transport spec section
  // "Listening for Messages from the Server". If the server returns HTTP 405 (Method Not
  // Allowed) for GET requests, this is treated as a supported configuration and the client
  // will fall back to POST-based message retrieval.
  bool enableGetListen = true;
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

  // Explicitly terminates the MCP session by sending HTTP DELETE.
  // Servers may return HTTP 405 if they don't support client-initiated termination.
  // Returns true if termination was successful (2xx response), false if server doesn't support it (405).
  auto terminateSession() -> bool;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace http

struct HttpClientOptions
{
  std::string endpointUrl;
  std::optional<std::string> bearerToken;
  http::ClientTlsConfiguration tls;
  security::RuntimeLimits limits;
  std::shared_ptr<http::SharedHeaderState> headerState = std::make_shared<http::SharedHeaderState>();
  std::optional<bool> enableLegacyHttpSseFallback;
  std::string legacyFallbackPostPath = "/rpc";
  std::string legacyFallbackSsePath = "/events";

  // Enable GET SSE listen behavior for server-initiated messages.
  // When enabled, the client will use HTTP GET requests to listen for server messages
  // via SSE (Server-Sent Events), as specified in MCP 2025-11-25 transport spec section
  // "Listening for Messages from the Server". If the server returns HTTP 405 (Method Not
  // Allowed) for GET requests, this is treated as a supported configuration and the client
  // will fall back to POST-based message retrieval.
  bool enableGetListen = true;
};

using HttpRequestHandler = std::function<http::ServerResponse(const http::ServerRequest &)>;

class HttpServerRuntime
{
public:
  explicit HttpServerRuntime(HttpServerOptions options = {});
  ~HttpServerRuntime();

  HttpServerRuntime(const HttpServerRuntime &) = delete;
  auto operator=(const HttpServerRuntime &) -> HttpServerRuntime & = delete;
  HttpServerRuntime(HttpServerRuntime &&other) noexcept;
  auto operator=(HttpServerRuntime &&other) noexcept -> HttpServerRuntime &;

  auto setRequestHandler(HttpRequestHandler handler) -> void;
  auto start() -> void;
  auto stop() noexcept -> void;
  [[nodiscard]] auto isRunning() const noexcept -> bool;
  [[nodiscard]] auto localPort() const noexcept -> std::uint16_t;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class HttpClientRuntime
{
public:
  explicit HttpClientRuntime(HttpClientOptions options);
  ~HttpClientRuntime();

  HttpClientRuntime(const HttpClientRuntime &) = delete;
  auto operator=(const HttpClientRuntime &) -> HttpClientRuntime & = delete;
  HttpClientRuntime(HttpClientRuntime &&other) noexcept;
  auto operator=(HttpClientRuntime &&other) noexcept -> HttpClientRuntime &;

  [[nodiscard]] auto execute(const http::ServerRequest &request) const -> http::ServerResponse;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp::transport
