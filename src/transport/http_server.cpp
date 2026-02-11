#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
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
  kGet,
  kPost,
};

static constexpr std::uint16_t kStatusOk = 200;
static constexpr std::uint16_t kStatusNoContent = 204;
static constexpr std::uint16_t kStatusAccepted = 202;
static constexpr std::uint16_t kStatusBadRequest = 400;
static constexpr std::uint16_t kStatusUnauthorized = 401;
static constexpr std::uint16_t kStatusForbidden = 403;
static constexpr std::uint16_t kStatusNotFound = 404;
static constexpr std::uint16_t kStatusMethodNotAllowed = 405;
static constexpr std::uint16_t kStatusConflict = 409;
static constexpr std::uint16_t kStatusInternalServerError = 500;
static constexpr bool kTerminateStream = true;
static constexpr bool kKeepStreamOpen = false;
static constexpr std::string_view kBearerScheme = "Bearer";
static constexpr std::string_view kWwwAuthenticateErrorInsufficientScope = "insufficient_scope";
static constexpr std::string_view kWellKnownOAuthProtectedResourcePath = "/.well-known/oauth-protected-resource";

enum class BearerTokenParseStatus : std::uint8_t
{
  kMissing,
  kInvalid,
  kPresent,
};

struct BearerTokenParseResult
{
  BearerTokenParseStatus status = BearerTokenParseStatus::kMissing;
  std::string token;
};

static auto requestMethodToString(ServerRequestMethod method) -> std::string
{
  switch (method)
  {
    case ServerRequestMethod::kGet:
      return "GET";
    case ServerRequestMethod::kPost:
      return "POST";
    case ServerRequestMethod::kDelete:
      return "DELETE";
  }

  return "POST";
}

static auto normalizeEndpointPath(std::string path) -> std::string
{
  if (path.empty())
  {
    path = "/mcp";
  }

  if (path.front() != '/')
  {
    path.insert(path.begin(), '/');
  }

  if (path.size() > 1 && path.back() == '/')
  {
    path.pop_back();
  }

  return path;
}

static auto pathBasedMetadataPathForEndpoint(std::string_view endpointPath) -> std::string
{
  const std::string normalizedEndpointPath = normalizeEndpointPath(std::string(endpointPath));
  if (normalizedEndpointPath == "/")
  {
    return std::string(kWellKnownOAuthProtectedResourcePath);
  }

  return std::string(kWellKnownOAuthProtectedResourcePath) + normalizedEndpointPath;
}

static auto parseBearerToken(const HeaderList &headers) -> BearerTokenParseResult
{
  const auto authorizationHeader = getHeader(headers, kHeaderAuthorization);
  if (!authorizationHeader.has_value())
  {
    return {};
  }

  const std::string_view trimmedAuthorization = detail::trimAsciiWhitespace(*authorizationHeader);
  if (trimmedAuthorization.empty())
  {
    BearerTokenParseResult result;
    result.status = BearerTokenParseStatus::kInvalid;
    return result;
  }

  const std::size_t schemeSeparator = trimmedAuthorization.find_first_of(" \t");
  if (schemeSeparator == std::string_view::npos)
  {
    BearerTokenParseResult result;
    result.status = BearerTokenParseStatus::kInvalid;
    return result;
  }

  const std::string_view scheme = trimmedAuthorization.substr(0, schemeSeparator);
  if (!detail::equalsIgnoreCase(scheme, kBearerScheme))
  {
    BearerTokenParseResult result;
    result.status = BearerTokenParseStatus::kInvalid;
    return result;
  }

  const std::string_view token = detail::trimAsciiWhitespace(trimmedAuthorization.substr(schemeSeparator + 1));
  if (token.empty())
  {
    BearerTokenParseResult result;
    result.status = BearerTokenParseStatus::kInvalid;
    return result;
  }

  const bool tokenHasWhitespace = std::any_of(token.begin(), token.end(), [](char character) -> bool { return std::isspace(static_cast<unsigned char>(character)) != 0; });
  if (tokenHasWhitespace)
  {
    BearerTokenParseResult result;
    result.status = BearerTokenParseStatus::kInvalid;
    return result;
  }

  BearerTokenParseResult result;
  result.status = BearerTokenParseStatus::kPresent;
  result.token = std::string(token);
  return result;
}

static auto sanitizeScopeSet(const auth::OAuthScopeSet &scopeSet) -> auth::OAuthScopeSet
{
  auth::OAuthScopeSet sanitized;
  for (const std::string &scope : scopeSet.values)
  {
    const std::string_view normalizedScope = detail::trimAsciiWhitespace(scope);
    if (normalizedScope.empty())
    {
      continue;
    }

    if (std::find(sanitized.values.begin(), sanitized.values.end(), normalizedScope) != sanitized.values.end())
    {
      continue;
    }

    sanitized.values.emplace_back(normalizedScope);
  }

  return sanitized;
}

static auto hasRequiredScopes(const auth::OAuthScopeSet &grantedScopes, const auth::OAuthScopeSet &requiredScopes) -> bool
{
  for (const std::string &requiredScope : requiredScopes.values)
  {
    if (std::find(grantedScopes.values.begin(), grantedScopes.values.end(), requiredScope) == grantedScopes.values.end())
    {
      return false;
    }
  }

  return true;
}

static auto joinScopeValues(const auth::OAuthScopeSet &scopeSet) -> std::string
{
  std::string joined;
  for (std::size_t index = 0; index < scopeSet.values.size(); ++index)
  {
    if (index > 0)
    {
      joined.push_back(' ');
    }

    joined += scopeSet.values[index];
  }

  return joined;
}

static auto escapeHttpQuotedString(std::string_view value) -> std::string
{
  std::string escaped;
  escaped.reserve(value.size());

  for (const char character : value)
  {
    if (character == '\\' || character == '"')
    {
      escaped.push_back('\\');
    }

    escaped.push_back(character);
  }

  return escaped;
}

static auto toProtectedResourceMetadataBody(const auth::OAuthProtectedResourceMetadata &metadata) -> std::string
{
  jsonrpc::JsonValue metadataJson = jsonrpc::JsonValue::object();
  metadataJson["resource"] = metadata.resource;

  metadataJson["authorization_servers"] = jsonrpc::JsonValue::array();
  for (const std::string &authorizationServer : metadata.authorizationServers)
  {
    metadataJson["authorization_servers"].push_back(authorizationServer);
  }

  if (!metadata.scopesSupported.values.empty())
  {
    metadataJson["scopes_supported"] = jsonrpc::JsonValue::array();
    for (const std::string &scope : metadata.scopesSupported.values)
    {
      metadataJson["scopes_supported"].push_back(scope);
    }
  }

  std::string encodedMetadata;
  metadataJson.dump(encodedMetadata);
  return encodedMetadata;
}

static auto metadataOriginForResource(std::string_view resource) -> std::optional<std::string>
{
  if (resource.empty() || resource.find('#') != std::string_view::npos)
  {
    return std::nullopt;
  }

  const std::size_t schemeSeparator = resource.find("://");
  if (schemeSeparator == std::string_view::npos)
  {
    return std::nullopt;
  }

  const std::string scheme = detail::toLowerAscii(resource.substr(0, schemeSeparator));
  if (scheme != "https")
  {
    return std::nullopt;
  }

  const std::size_t authorityBegin = schemeSeparator + 3;
  if (authorityBegin >= resource.size())
  {
    return std::nullopt;
  }

  const std::size_t pathBegin = resource.find('/', authorityBegin);
  const std::size_t queryBegin = resource.find('?', authorityBegin);

  std::size_t authorityEnd = resource.size();
  if (pathBegin != std::string_view::npos)
  {
    authorityEnd = std::min(authorityEnd, pathBegin);
  }

  if (queryBegin != std::string_view::npos)
  {
    authorityEnd = std::min(authorityEnd, queryBegin);
  }

  if (authorityEnd <= authorityBegin)
  {
    return std::nullopt;
  }

  return std::string(resource.substr(0, authorityEnd));
}

static auto makeUnauthorizedChallenge(std::string_view resourceMetadataUrl, const auth::OAuthScopeSet &requiredScopes) -> std::string
{
  std::string challenge = std::string(kBearerScheme) + " resource_metadata=\"" + escapeHttpQuotedString(resourceMetadataUrl) + "\"";
  const std::string scopeValue = joinScopeValues(requiredScopes);
  if (!scopeValue.empty())
  {
    challenge += ", scope=\"" + escapeHttpQuotedString(scopeValue) + "\"";
  }

  return challenge;
}

static auto makeInsufficientScopeChallenge(std::string_view resourceMetadataUrl, const auth::OAuthScopeSet &requiredScopes) -> std::string
{
  const std::string scopeValue = joinScopeValues(requiredScopes);
  std::string challenge = std::string(kBearerScheme) + " error=\"" + std::string(kWwwAuthenticateErrorInsufficientScope) + "\"";
  challenge += ", scope=\"" + escapeHttpQuotedString(scopeValue) + "\"";
  challenge += ", resource_metadata=\"" + escapeHttpQuotedString(resourceMetadataUrl) + "\"";
  return challenge;
}

struct StreamEventRecord
{
  std::uint64_t cursor = 0;
  mcp::http::sse::Event event;
};

struct StreamState
{
  std::string streamId;
  std::optional<std::string> sessionId;
  StreamKind kind = StreamKind::kGet;
  std::uint64_t nextCursor = 1;
  std::chrono::steady_clock::time_point openedAt = std::chrono::steady_clock::now();
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
  if (validation.authorizationContext.has_value())
  {
    context.authContext = validation.authorizationContext->taskIsolationKey;
  }

  return context;
}

struct StreamableHttpServer::Impl
{
  explicit Impl(StreamableHttpServerOptions options)
    : options(std::move(options))
  {
    initializeAuthorizationConfiguration();
  }

  auto initializeAuthorizationConfiguration() -> void
  {
    if (!options.http.authorization.has_value())
    {
      return;
    }

    auth::OAuthServerAuthorizationOptions &authorization = *options.http.authorization;

    if (!authorization.tokenVerifier)
    {
      throw std::invalid_argument("HTTP server authorization requires a token verifier");
    }

    authorization.defaultRequiredScopes = sanitizeScopeSet(authorization.defaultRequiredScopes);
    authorization.protectedResourceMetadata.scopesSupported = sanitizeScopeSet(authorization.protectedResourceMetadata.scopesSupported);

    if (authorization.protectedResourceMetadata.resource.empty())
    {
      throw std::invalid_argument("HTTP server authorization requires protected resource metadata.resource");
    }

    if (authorization.protectedResourceMetadata.resource.find('#') != std::string::npos)
    {
      throw std::invalid_argument("Protected resource metadata.resource must not contain a fragment");
    }

    if (authorization.protectedResourceMetadata.authorizationServers.empty())
    {
      throw std::invalid_argument("HTTP server authorization requires at least one authorization server");
    }

    if (!authorization.metadataPublication.publishAtPathBasedWellKnownUri && !authorization.metadataPublication.publishAtRootWellKnownUri)
    {
      throw std::invalid_argument("HTTP server authorization must publish metadata at path-based and/or root well-known URI");
    }

    rootMetadataPath = std::string(kWellKnownOAuthProtectedResourcePath);
    pathBasedMetadataPath = pathBasedMetadataPathForEndpoint(options.http.endpoint.path);
    protectedResourceMetadataBody = toProtectedResourceMetadataBody(authorization.protectedResourceMetadata);

    if (authorization.metadataPublication.challengeResourceMetadataUrl.has_value())
    {
      const std::string_view challengeUrl = detail::trimAsciiWhitespace(*authorization.metadataPublication.challengeResourceMetadataUrl);
      if (challengeUrl.empty())
      {
        throw std::invalid_argument("HTTP server authorization challengeResourceMetadataUrl must not be empty");
      }

      if (!metadataOriginForResource(challengeUrl).has_value())
      {
        throw std::invalid_argument("HTTP server authorization challengeResourceMetadataUrl must be an absolute HTTPS URL without fragments");
      }

      challengeResourceMetadataUrl = std::string(challengeUrl);
      return;
    }

    const auto metadataOrigin = metadataOriginForResource(authorization.protectedResourceMetadata.resource);
    if (!metadataOrigin.has_value())
    {
      throw std::invalid_argument("HTTP server authorization protected resource metadata.resource must be an absolute HTTPS URL without fragments");
    }

    const bool usePathBasedMetadataChallenge = authorization.metadataPublication.publishAtPathBasedWellKnownUri;
    challengeResourceMetadataUrl = *metadataOrigin + (usePathBasedMetadataChallenge ? pathBasedMetadataPath : rootMetadataPath);
  }

  auto makeAuthorizationRequestContext(const ServerRequest &request, const std::optional<std::string> &sessionId) const -> auth::OAuthAuthorizationRequestContext
  {
    auth::OAuthAuthorizationRequestContext authorizationRequest;
    authorizationRequest.httpMethod = requestMethodToString(request.method);
    authorizationRequest.httpPath = request.path;
    authorizationRequest.sessionId = sessionId;
    return authorizationRequest;
  }

  auto resolveRequiredScopes(const ServerRequest &request, const std::optional<std::string> &sessionId) const -> auth::OAuthScopeSet
  {
    if (!options.http.authorization.has_value())
    {
      return {};
    }

    const auth::OAuthServerAuthorizationOptions &authorization = *options.http.authorization;
    if (authorization.requiredScopesResolver)
    {
      return sanitizeScopeSet(authorization.requiredScopesResolver(makeAuthorizationRequestContext(request, sessionId)));
    }

    return sanitizeScopeSet(authorization.defaultRequiredScopes);
  }

  auto unauthorizedResponse(const auth::OAuthScopeSet &requiredScopes) const -> ServerResponse
  {
    ServerResponse response = statusResponse(kStatusUnauthorized);
    setHeader(response.headers, kHeaderWwwAuthenticate, makeUnauthorizedChallenge(challengeResourceMetadataUrl, requiredScopes));
    return response;
  }

  auto insufficientScopeResponse(const auth::OAuthScopeSet &requiredScopes) const -> ServerResponse
  {
    ServerResponse response = statusResponse(kStatusForbidden);
    setHeader(response.headers, kHeaderWwwAuthenticate, makeInsufficientScopeChallenge(challengeResourceMetadataUrl, requiredScopes));
    return response;
  }

  auto authorizeRequest(const ServerRequest &request, RequestValidationResult &validation) const -> std::optional<ServerResponse>
  {
    if (!options.http.authorization.has_value())
    {
      return std::nullopt;
    }

    const auth::OAuthServerAuthorizationOptions &authorization = *options.http.authorization;
    const auth::OAuthScopeSet requiredScopes = resolveRequiredScopes(request, validation.sessionId);

    const BearerTokenParseResult bearerToken = parseBearerToken(request.headers);
    if (bearerToken.status != BearerTokenParseStatus::kPresent)
    {
      return unauthorizedResponse(requiredScopes);
    }

    auth::OAuthTokenVerificationRequest verificationRequest;
    verificationRequest.bearerToken = bearerToken.token;
    verificationRequest.expectedAudience = authorization.protectedResourceMetadata.resource;
    verificationRequest.request = makeAuthorizationRequestContext(request, validation.sessionId);
    verificationRequest.requiredScopes = requiredScopes;

    auth::OAuthTokenVerificationResult verificationResult;
    try
    {
      verificationResult = authorization.tokenVerifier->verifyToken(verificationRequest);
    }
    catch (...)
    {
      return unauthorizedResponse(requiredScopes);
    }

    verificationResult.authorizationContext.grantedScopes = sanitizeScopeSet(verificationResult.authorizationContext.grantedScopes);

    if (verificationResult.status == auth::OAuthTokenVerificationStatus::kInvalidToken || !verificationResult.audienceBound
        || verificationResult.authorizationContext.taskIsolationKey.empty())
    {
      return unauthorizedResponse(requiredScopes);
    }

    const bool missingRequiredScopes = !hasRequiredScopes(verificationResult.authorizationContext.grantedScopes, requiredScopes);
    if (verificationResult.status == auth::OAuthTokenVerificationStatus::kInsufficientScope || missingRequiredScopes)
    {
      return insufficientScopeResponse(requiredScopes);
    }

    validation.authorizationContext = verificationResult.authorizationContext;
    return std::nullopt;
  }

  auto handleProtectedResourceMetadataRequest(const ServerRequest &request) const -> std::optional<ServerResponse>
  {
    if (!options.http.authorization.has_value())
    {
      return std::nullopt;
    }

    const auth::OAuthServerAuthorizationOptions &authorization = *options.http.authorization;
    const bool isRootMetadataPath = request.path == rootMetadataPath;
    const bool isPathMetadataPath = request.path == pathBasedMetadataPath;

    if (!isRootMetadataPath && !isPathMetadataPath)
    {
      return std::nullopt;
    }

    const bool rootPublished = isRootMetadataPath && authorization.metadataPublication.publishAtRootWellKnownUri;
    const bool pathPublished = isPathMetadataPath && authorization.metadataPublication.publishAtPathBasedWellKnownUri;
    if (!rootPublished && !pathPublished)
    {
      return statusResponse(kStatusNotFound);
    }

    if (request.method != ServerRequestMethod::kGet)
    {
      return statusResponse(kStatusMethodNotAllowed);
    }

    return jsonResponse(kStatusOk, protectedResourceMetadataBody);
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

  [[nodiscard]] auto isStreamExpired(const StreamState &stream) const -> bool
  {
    if (options.http.limits.maxSseStreamDuration.count() <= 0)
    {
      return false;
    }

    const auto elapsed = std::chrono::steady_clock::now() - stream.openedAt;
    return elapsed > options.http.limits.maxSseStreamDuration;
  }

  auto applySseBufferLimit(StreamState &stream) const -> void
  {
    const std::size_t maxBufferedMessages = options.http.limits.maxSseBufferedMessages;
    if (maxBufferedMessages == 0)
    {
      stream.events.clear();
      return;
    }

    if (stream.events.size() <= maxBufferedMessages)
    {
      return;
    }

    const std::size_t overflow = stream.events.size() - maxBufferedMessages;
    stream.events.erase(stream.events.begin(), stream.events.begin() + static_cast<std::ptrdiff_t>(overflow));
  }

  auto appendPrimingEvent(StreamState &stream) const -> void
  {
    const std::uint64_t cursor = stream.nextCursor++;
    std::string eventId = mcp::http::sse::makeEventId(stream.streamId, cursor);
    stream.events.push_back(StreamEventRecord {cursor, makeEvent(std::move(eventId), "")});
    applySseBufferLimit(stream);
  }

  auto appendMessageEvent(StreamState &stream, const jsonrpc::Message &message) const -> bool
  {
    const std::string serializedMessage = jsonrpc::serializeMessage(message);
    if (serializedMessage.size() > options.http.limits.maxMessageSizeBytes)
    {
      return false;
    }

    const std::uint64_t cursor = stream.nextCursor++;
    std::string eventId = mcp::http::sse::makeEventId(stream.streamId, cursor);
    stream.events.push_back(StreamEventRecord {cursor, makeEvent(std::move(eventId), serializedMessage)});
    applySseBufferLimit(stream);
    return true;
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

    // NOLINTNEXTLINE(misc-const-correctness)
    StreamState *postFallback = nullptr;
    for (const std::string &streamId : streamOrder)
    {
      auto streamIt = streams.find(streamId);
      if (streamIt == streams.end())
      {
        continue;
      }

      StreamState &stream = streamIt->second;
      if (isStreamExpired(stream))
      {
        stream.terminated = true;
      }

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
        if (stream.kind == StreamKind::kPost)
        {
          return &stream;
        }

        continue;
      }

      if (stream.kind == StreamKind::kGet)
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
      static_cast<void>(appendMessageEvent(stream, message));
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

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  auto handlePost(const ServerRequest &request) -> ServerResponse
  {
    if (request.body.size() > options.http.limits.maxMessageSizeBytes)
    {
      return jsonResponse(kStatusBadRequest, makeJsonRpcErrorBody("Request body exceeds configured max message size."));
    }

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
    RequestValidationResult validation = validate(request, requestKind, options.http.requireSessionId);
    if (!validation.accepted)
    {
      return rejectValidation(validation);
    }

    const std::optional<ServerResponse> authorizationRejection = authorizeRequest(request, validation);
    if (authorizationRejection.has_value())
    {
      return *authorizationRejection;
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

    StreamState &stream = createStream(validation.sessionId, StreamKind::kPost);
    for (const jsonrpc::Message &preResponse : requestResult.preResponseMessages)
    {
      if (!appendMessageEvent(stream, preResponse))
      {
        stream.terminated = true;
        return jsonResponse(kStatusBadRequest, makeJsonRpcErrorBody("SSE message exceeds configured max message size."));
      }
    }

    if (requestResult.response.has_value())
    {
      const jsonrpc::Message responseMessage = std::visit([](const auto &typedResponse) -> jsonrpc::Message { return jsonrpc::Message {typedResponse}; }, *requestResult.response);
      if (!appendMessageEvent(stream, responseMessage))
      {
        stream.terminated = true;
        return jsonResponse(kStatusBadRequest, makeJsonRpcErrorBody("SSE response exceeds configured max message size."));
      }
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

    RequestValidationResult validation = validate(request, RequestKind::kOther, options.http.requireSessionId);
    if (!validation.accepted)
    {
      return rejectValidation(validation);
    }

    const std::optional<ServerResponse> authorizationRejection = authorizeRequest(request, validation);
    if (authorizationRejection.has_value())
    {
      return *authorizationRejection;
    }

    const auto lastEventId = getHeader(request.headers, kHeaderLastEventId);
    if (!lastEventId.has_value())
    {
      StreamState &stream = createStream(validation.sessionId, StreamKind::kGet);
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
    if (isStreamExpired(stream))
    {
      stream.terminated = true;
      return statusResponse(kStatusNotFound);
    }

    if (stream.sessionId != validation.sessionId)
    {
      return statusResponse(kStatusNotFound);
    }

    if (!stream.events.empty())
    {
      const std::uint64_t oldestRetainedCursor = stream.events.front().cursor;
      if (eventCursor->cursor + 1 < oldestRetainedCursor)
      {
        return jsonResponse(kStatusConflict, makeJsonRpcErrorBody("Last-Event-ID is outside the retained SSE buffer window."));
      }
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
    RequestValidationResult validation = validate(request, RequestKind::kOther, sessionRequired);
    if (!validation.accepted)
    {
      return rejectValidation(validation);
    }

    const std::optional<ServerResponse> authorizationRejection = authorizeRequest(request, validation);
    if (authorizationRejection.has_value())
    {
      return *authorizationRejection;
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
    const std::optional<ServerResponse> protectedResourceMetadataResponse = handleProtectedResourceMetadataRequest(request);
    if (protectedResourceMetadataResponse.has_value())
    {
      return *protectedResourceMetadataResponse;
    }

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
      if (!appendMessageEvent(*stream, message))
      {
        return false;
      }

      if (isResponseMessage(message) && stream->kind == StreamKind::kPost)
      {
        stream->terminated = true;
      }

      return true;
    }

    auto &pendingMessages = pendingMessagesBySession[sessionKey(sessionId)];
    if (pendingMessages.size() >= options.http.limits.maxSseBufferedMessages)
    {
      return false;
    }

    pendingMessages.push_back(message);
    return true;
  }

  std::string rootMetadataPath = std::string(kWellKnownOAuthProtectedResourcePath);
  std::string pathBasedMetadataPath = std::string(kWellKnownOAuthProtectedResourcePath);
  std::string protectedResourceMetadataBody;
  std::string challengeResourceMetadataUrl;

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
