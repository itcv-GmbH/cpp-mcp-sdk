#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mcp/auth/client_registration.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/transport/http.hpp>

// NOLINTBEGIN(llvm-prefer-static-over-anonymous-namespace, readability-static-definition-in-anonymous-namespace)

namespace mcp::auth
{
namespace
{

using transport::http::ServerRequestMethod;

constexpr std::uint16_t kHttpStatusOk = 200;
constexpr std::uint16_t kHttpStatusCreated = 201;

struct ParsedUrl
{
  std::string scheme;
  std::string authority;
  std::string path;
};

static auto trimAsciiWhitespace(std::string_view value) -> std::string_view
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

static auto toLowerAscii(std::string_view value) -> std::string
{
  std::string normalized;
  normalized.reserve(value.size());
  for (const char character : value)
  {
    normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }

  return normalized;
}

static auto containsAsciiWhitespaceOrControl(std::string_view value) -> bool
{
  return std::any_of(value.begin(),
                     value.end(),
                     [](char character) -> bool
                     {
                       const auto byte = static_cast<unsigned char>(character);
                       return std::isspace(byte) != 0 || std::iscntrl(byte) != 0;
                     });
}

static auto validateAbsoluteUrl(std::string_view rawUrl, std::string_view fieldName, ClientRegistrationErrorCode errorCode) -> ParsedUrl
{
  const std::string_view trimmedUrl = trimAsciiWhitespace(rawUrl);
  if (trimmedUrl.empty())
  {
    throw ClientRegistrationError(errorCode, std::string(fieldName) + " must not be empty");
  }

  if (containsAsciiWhitespaceOrControl(trimmedUrl))
  {
    throw ClientRegistrationError(errorCode, std::string(fieldName) + " must not contain whitespace or control characters");
  }

  if (trimmedUrl.find('#') != std::string_view::npos)
  {
    throw ClientRegistrationError(errorCode, std::string(fieldName) + " must not contain URL fragments");
  }

  const std::size_t schemeSeparator = trimmedUrl.find("://");
  if (schemeSeparator == std::string_view::npos || schemeSeparator == 0)
  {
    throw ClientRegistrationError(errorCode, std::string(fieldName) + " must be an absolute URL");
  }

  const std::string_view schemeText = trimmedUrl.substr(0, schemeSeparator);
  if (std::isalpha(static_cast<unsigned char>(schemeText.front())) == 0)
  {
    throw ClientRegistrationError(errorCode, std::string(fieldName) + " scheme must begin with an ASCII letter");
  }

  const bool validScheme = std::all_of(schemeText.begin(),
                                       schemeText.end(),
                                       [](char character) -> bool
                                       {
                                         const auto byte = static_cast<unsigned char>(character);
                                         return std::isalnum(byte) != 0 || character == '+' || character == '-' || character == '.';
                                       });
  if (!validScheme)
  {
    throw ClientRegistrationError(errorCode, std::string(fieldName) + " scheme contains invalid characters");
  }

  const std::size_t authorityBegin = schemeSeparator + 3;
  if (authorityBegin >= trimmedUrl.size())
  {
    throw ClientRegistrationError(errorCode, std::string(fieldName) + " must include a host");
  }

  std::size_t authorityEnd = trimmedUrl.find_first_of("/?", authorityBegin);
  if (authorityEnd == std::string_view::npos)
  {
    authorityEnd = trimmedUrl.size();
  }

  const std::string_view authority = trimmedUrl.substr(authorityBegin, authorityEnd - authorityBegin);
  if (authority.empty() || authority.find('@') != std::string_view::npos)
  {
    throw ClientRegistrationError(errorCode, std::string(fieldName) + " authority is invalid");
  }

  ParsedUrl parsed;
  parsed.scheme = toLowerAscii(schemeText);
  parsed.authority = std::string(authority);
  parsed.path = "/";

  if (authorityEnd < trimmedUrl.size())
  {
    const char delimiter = trimmedUrl[authorityEnd];
    if (delimiter == '/')
    {
      std::size_t pathEnd = trimmedUrl.find('?', authorityEnd);
      if (pathEnd == std::string_view::npos)
      {
        pathEnd = trimmedUrl.size();
      }

      parsed.path = std::string(trimmedUrl.substr(authorityEnd, pathEnd - authorityEnd));
    }
    else if (delimiter != '?')
    {
      throw ClientRegistrationError(errorCode, std::string(fieldName) + " path is invalid");
    }
  }

  return parsed;
}

static auto sanitizeStringList(const std::vector<std::string> &values, std::string_view fieldName, ClientRegistrationErrorCode errorCode, bool requireNonEmpty)
  -> std::vector<std::string>
{
  std::vector<std::string> sanitized;
  for (const std::string &value : values)
  {
    const std::string_view trimmed = trimAsciiWhitespace(value);
    if (trimmed.empty())
    {
      continue;
    }

    if (containsAsciiWhitespaceOrControl(trimmed))
    {
      throw ClientRegistrationError(errorCode, std::string(fieldName) + " contains invalid whitespace or control characters");
    }

    if (std::find(sanitized.begin(), sanitized.end(), trimmed) == sanitized.end())
    {
      sanitized.emplace_back(trimmed);
    }
  }

  if (requireNonEmpty && sanitized.empty())
  {
    throw ClientRegistrationError(errorCode, std::string(fieldName) + " must contain at least one value");
  }

  return sanitized;
}

static auto requireTrimmedValue(std::string_view value, std::string_view fieldName, ClientRegistrationErrorCode errorCode) -> std::string
{
  const std::string_view trimmed = trimAsciiWhitespace(value);
  if (trimmed.empty())
  {
    throw ClientRegistrationError(errorCode, std::string(fieldName) + " must not be empty");
  }

  if (containsAsciiWhitespaceOrControl(trimmed))
  {
    throw ClientRegistrationError(errorCode, std::string(fieldName) + " contains invalid whitespace or control characters");
  }

  return std::string(trimmed);
}

static auto toTokenEndpointAuthMethod(ClientAuthenticationMethod method) -> std::string
{
  switch (method)
  {
    case ClientAuthenticationMethod::kNone:
      return "none";
    case ClientAuthenticationMethod::kClientSecretBasic:
      return "client_secret_basic";
    case ClientAuthenticationMethod::kClientSecretPost:
      return "client_secret_post";
    case ClientAuthenticationMethod::kPrivateKeyJwt:
      return "private_key_jwt";
  }

  return "none";
}

static auto parseTokenEndpointAuthMethod(std::string_view methodText, ClientRegistrationErrorCode errorCode) -> ClientAuthenticationMethod
{
  const std::string normalized = toLowerAscii(trimAsciiWhitespace(methodText));
  if (normalized.empty() || normalized == "none")
  {
    return ClientAuthenticationMethod::kNone;
  }

  if (normalized == "client_secret_basic")
  {
    return ClientAuthenticationMethod::kClientSecretBasic;
  }

  if (normalized == "client_secret_post")
  {
    return ClientAuthenticationMethod::kClientSecretPost;
  }

  if (normalized == "private_key_jwt")
  {
    return ClientAuthenticationMethod::kPrivateKeyJwt;
  }

  throw ClientRegistrationError(errorCode, "Unsupported token_endpoint_auth_method: '" + std::string(methodText) + "'");
}

static auto methodRequiresClientSecret(ClientAuthenticationMethod method) -> bool
{
  return method == ClientAuthenticationMethod::kClientSecretBasic || method == ClientAuthenticationMethod::kClientSecretPost;
}

static auto parseJsonObject(std::string_view document, std::string_view context, ClientRegistrationErrorCode errorCode) -> jsonrpc::JsonValue
{
  try
  {
    jsonrpc::JsonValue parsed = jsonrpc::JsonValue::parse(std::string(document));
    if (!parsed.is_object())
    {
      throw ClientRegistrationError(errorCode, std::string(context) + " must be a JSON object");
    }

    return parsed;
  }
  catch (const ClientRegistrationError &)
  {
    throw;
  }
  catch (const std::exception &error)
  {
    throw ClientRegistrationError(errorCode, "Failed to parse " + std::string(context) + ": " + error.what());
  }
}

static auto parseOptionalStringArray(const jsonrpc::JsonValue &object, std::string_view fieldName, ClientRegistrationErrorCode errorCode, bool required, bool requireNonEmpty)
  -> std::vector<std::string>
{
  if (!object.contains(std::string(fieldName)))
  {
    if (required)
    {
      throw ClientRegistrationError(errorCode, std::string(fieldName) + " must be present");
    }

    return {};
  }

  const jsonrpc::JsonValue &fieldValue = object[std::string(fieldName)];
  if (!fieldValue.is_array())
  {
    throw ClientRegistrationError(errorCode, std::string(fieldName) + " must be an array");
  }

  std::vector<std::string> values;
  for (const auto &entry : fieldValue.array_range())
  {
    if (!entry.is_string())
    {
      throw ClientRegistrationError(errorCode, std::string(fieldName) + " entries must be strings");
    }

    values.push_back(entry.as<std::string>());
  }

  return sanitizeStringList(values, fieldName, errorCode, requireNonEmpty);
}

static auto parseOptionalStringField(const jsonrpc::JsonValue &object, std::string_view fieldName, ClientRegistrationErrorCode errorCode) -> std::optional<std::string>
{
  if (!object.contains(std::string(fieldName)))
  {
    return std::nullopt;
  }

  const jsonrpc::JsonValue &fieldValue = object[std::string(fieldName)];
  if (!fieldValue.is_string())
  {
    throw ClientRegistrationError(errorCode, std::string(fieldName) + " must be a string when present");
  }

  const std::string fieldText = fieldValue.as<std::string>();
  const std::string_view trimmed = trimAsciiWhitespace(fieldText);
  if (trimmed.empty())
  {
    return std::nullopt;
  }

  return std::string(trimmed);
}

static auto parseRequestMethod(std::string_view method) -> ServerRequestMethod
{
  const std::string normalized = toLowerAscii(trimAsciiWhitespace(method));
  if (normalized == "post")
  {
    return ServerRequestMethod::kPost;
  }

  if (normalized == "get")
  {
    return ServerRequestMethod::kGet;
  }

  if (normalized == "delete")
  {
    return ServerRequestMethod::kDelete;
  }

  throw ClientRegistrationError(ClientRegistrationErrorCode::kInvalidInput, "Unsupported registration HTTP method: '" + std::string(method) + "'");
}

static auto defaultHttpExecutor(const ClientRegistrationHttpRequest &request) -> ClientRegistrationHttpResponse
{
  transport::HttpClientOptions options;
  options.endpointUrl = request.url;
  options.tls.verifyPeer = true;

  const transport::HttpClientRuntime runtime(std::move(options));

  transport::http::ServerRequest runtimeRequest;
  runtimeRequest.method = parseRequestMethod(request.method);
  runtimeRequest.path.clear();
  runtimeRequest.body = request.body;
  for (const ClientRegistrationHeader &header : request.headers)
  {
    runtimeRequest.headers.push_back(transport::http::Header {header.name, header.value});
  }

  const transport::http::ServerResponse runtimeResponse = runtime.execute(runtimeRequest);

  ClientRegistrationHttpResponse response;
  response.statusCode = runtimeResponse.statusCode;
  response.body = runtimeResponse.body;
  response.headers.reserve(runtimeResponse.headers.size());
  for (const transport::http::Header &header : runtimeResponse.headers)
  {
    response.headers.push_back(ClientRegistrationHeader {header.name, header.value});
  }

  return response;
}

static auto resolvePreRegistered(const PreRegisteredClientConfiguration &configuration) -> ClientRegistrationResult
{
  ClientRegistrationResult result;
  result.strategy = ClientRegistrationStrategy::kPreRegistered;
  result.client.clientId = requireTrimmedValue(configuration.clientId, "pre-registered client_id", ClientRegistrationErrorCode::kInvalidInput);
  result.client.redirectUris = sanitizeStringList(configuration.redirectUris,
                                                  "pre-registered redirect_uris",
                                                  ClientRegistrationErrorCode::kInvalidInput,
                                                  /*requireNonEmpty=*/true);
  result.client.authenticationMethod = configuration.authenticationMethod;
  result.client.clientSecret = configuration.clientSecret;

  if (methodRequiresClientSecret(result.client.authenticationMethod) && (!result.client.clientSecret.has_value() || trimAsciiWhitespace(*result.client.clientSecret).empty()))
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kInvalidInput, "Pre-registered client authentication method requires a non-empty client_secret");
  }

  return result;
}

static auto resolveClientIdMetadataDocument(const ClientIdMetadataDocumentConfiguration &configuration) -> ClientRegistrationResult
{
  validateClientIdMetadataDocumentClientIdUrl(configuration.clientId);

  if (trimAsciiWhitespace(configuration.clientName).empty())
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kInvalidInput, "Client ID metadata document configuration requires client_name");
  }

  if (methodRequiresClientSecret(configuration.authenticationMethod))
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kInvalidInput,
                                  "Client ID metadata document registration does not support client_secret authentication methods without host-managed secrets");
  }

  ClientRegistrationResult result;
  result.strategy = ClientRegistrationStrategy::kClientIdMetadataDocument;
  result.client.clientId = std::string(trimAsciiWhitespace(configuration.clientId));
  result.client.redirectUris = sanitizeStringList(configuration.redirectUris,
                                                  "Client ID metadata document redirect_uris",
                                                  ClientRegistrationErrorCode::kInvalidInput,
                                                  /*requireNonEmpty=*/true);
  result.client.authenticationMethod = configuration.authenticationMethod;
  return result;
}

static auto buildDynamicClientRegistrationPayload(const DynamicClientRegistrationConfiguration &configuration) -> std::string
{
  jsonrpc::JsonValue payload = jsonrpc::JsonValue::object();

  if (configuration.clientName.has_value())
  {
    const std::string_view trimmedClientName = trimAsciiWhitespace(*configuration.clientName);
    if (trimmedClientName.empty())
    {
      throw ClientRegistrationError(ClientRegistrationErrorCode::kInvalidInput, "Dynamic registration client_name must not be empty when provided");
    }

    payload["client_name"] = std::string(trimmedClientName);
  }

  const std::vector<std::string> redirectUris = sanitizeStringList(configuration.redirectUris,
                                                                   "dynamic registration redirect_uris",
                                                                   ClientRegistrationErrorCode::kInvalidInput,
                                                                   /*requireNonEmpty=*/false);
  if (!redirectUris.empty())
  {
    payload["redirect_uris"] = jsonrpc::JsonValue::array();
    for (const std::string &redirectUri : redirectUris)
    {
      payload["redirect_uris"].push_back(redirectUri);
    }
  }

  payload["grant_types"] = jsonrpc::JsonValue::array();
  payload["grant_types"].push_back("authorization_code");

  payload["response_types"] = jsonrpc::JsonValue::array();
  payload["response_types"].push_back("code");

  payload["token_endpoint_auth_method"] = toTokenEndpointAuthMethod(configuration.authenticationMethod);

  std::string encodedPayload;
  payload.dump(encodedPayload);
  return encodedPayload;
}

static auto parseDynamicClientRegistrationResponse(const ClientRegistrationHttpResponse &response, const DynamicClientRegistrationConfiguration &configuration)
  -> ResolvedClientIdentity
{
  if (response.statusCode != kHttpStatusOk && response.statusCode != kHttpStatusCreated)
  {
    std::ostringstream stream;
    stream << "Dynamic client registration failed with HTTP status " << response.statusCode;
    throw ClientRegistrationError(ClientRegistrationErrorCode::kNetworkFailure, stream.str());
  }

  const jsonrpc::JsonValue parsedResponse = parseJsonObject(response.body, "dynamic client registration response", ClientRegistrationErrorCode::kMetadataValidation);

  if (!parsedResponse.contains("client_id") || !parsedResponse["client_id"].is_string())
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kMetadataValidation, "Dynamic client registration response.client_id must be a string");
  }

  ResolvedClientIdentity identity;
  identity.clientId =
    requireTrimmedValue(parsedResponse["client_id"].as<std::string>(), "dynamic registration response.client_id", ClientRegistrationErrorCode::kMetadataValidation);

  identity.redirectUris = parseOptionalStringArray(parsedResponse,
                                                   "redirect_uris",
                                                   ClientRegistrationErrorCode::kMetadataValidation,
                                                   /*required=*/false,
                                                   /*requireNonEmpty=*/false);
  if (identity.redirectUris.empty())
  {
    identity.redirectUris = sanitizeStringList(configuration.redirectUris,
                                               "dynamic registration redirect_uris",
                                               ClientRegistrationErrorCode::kInvalidInput,
                                               /*requireNonEmpty=*/false);
  }

  if (identity.redirectUris.empty())
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kMetadataValidation, "Dynamic client registration did not return redirect_uris and no defaults were configured");
  }

  const auto responseAuthMethod = parseOptionalStringField(parsedResponse, "token_endpoint_auth_method", ClientRegistrationErrorCode::kMetadataValidation);
  if (responseAuthMethod.has_value())
  {
    identity.authenticationMethod = parseTokenEndpointAuthMethod(*responseAuthMethod, ClientRegistrationErrorCode::kMetadataValidation);
  }
  else if (configuration.authenticationMethod != ClientAuthenticationMethod::kNone)
  {
    identity.authenticationMethod = configuration.authenticationMethod;
  }
  else
  {
    identity.authenticationMethod = ClientAuthenticationMethod::kNone;
  }

  if (const auto clientSecret = parseOptionalStringField(parsedResponse, "client_secret", ClientRegistrationErrorCode::kMetadataValidation); clientSecret.has_value())
  {
    identity.clientSecret = *clientSecret;
  }

  if (methodRequiresClientSecret(identity.authenticationMethod) && (!identity.clientSecret.has_value() || trimAsciiWhitespace(*identity.clientSecret).empty()))
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kMetadataValidation,
                                  "Dynamic client registration response selected a client_secret authentication method without returning client_secret");
  }

  return identity;
}

static auto resolveDynamic(const ResolveClientRegistrationRequest &request) -> ClientRegistrationResult
{
  const auto registrationEndpoint = request.authorizationServerMetadata.registrationEndpoint;
  if (!registrationEndpoint.has_value() || trimAsciiWhitespace(*registrationEndpoint).empty())
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kHostInteractionRequired, "Authorization server does not advertise registration_endpoint for dynamic registration");
  }

  const ParsedUrl parsedRegistrationEndpoint = validateAbsoluteUrl(*registrationEndpoint, "registration_endpoint", ClientRegistrationErrorCode::kMetadataValidation);
  if (parsedRegistrationEndpoint.scheme != "https")
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kMetadataValidation, "registration_endpoint must use HTTPS for dynamic client registration");
  }

  const std::string issuer = requireTrimmedValue(request.authorizationServerMetadata.issuer, "authorization server issuer", ClientRegistrationErrorCode::kInvalidInput);

  std::shared_ptr<ClientCredentialsStore> credentialsStore = request.credentialsStore;
  if (!credentialsStore)
  {
    credentialsStore = std::make_shared<InMemoryClientCredentialsStore>();
  }

  if (const auto cachedIdentity = credentialsStore->load(issuer); cachedIdentity.has_value())
  {
    ClientRegistrationResult result;
    result.strategy = ClientRegistrationStrategy::kDynamic;
    result.client = *cachedIdentity;
    return result;
  }

  ClientRegistrationHttpExecutor executor = request.httpExecutor;
  if (!executor)
  {
    executor = defaultHttpExecutor;
  }

  ClientRegistrationHttpRequest httpRequest;
  httpRequest.method = "POST";
  httpRequest.url = std::string(trimAsciiWhitespace(*registrationEndpoint));
  httpRequest.headers.push_back(ClientRegistrationHeader {"Content-Type", "application/json"});
  httpRequest.headers.push_back(ClientRegistrationHeader {"Accept", "application/json"});
  httpRequest.body = buildDynamicClientRegistrationPayload(request.strategyConfiguration.dynamic);

  const ClientRegistrationHttpResponse httpResponse = executor(httpRequest);
  const ResolvedClientIdentity dynamicIdentity = parseDynamicClientRegistrationResponse(httpResponse, request.strategyConfiguration.dynamic);
  credentialsStore->save(issuer, dynamicIdentity);

  ClientRegistrationResult result;
  result.strategy = ClientRegistrationStrategy::kDynamic;
  result.client = dynamicIdentity;
  return result;
}

}  // namespace

ClientRegistrationError::ClientRegistrationError(ClientRegistrationErrorCode code, const std::string &message)
  : std::runtime_error(message)
  , code_(code)
{
}

auto ClientRegistrationError::code() const noexcept -> ClientRegistrationErrorCode
{
  return code_;
}

auto InMemoryClientCredentialsStore::load(std::string_view authorizationServerIssuer) const -> std::optional<ResolvedClientIdentity>
{
  const std::scoped_lock lock(mutex_);
  const auto found = credentialsByIssuer_.find(std::string(authorizationServerIssuer));
  if (found == credentialsByIssuer_.end())
  {
    return std::nullopt;
  }

  return found->second;
}

auto InMemoryClientCredentialsStore::save(std::string authorizationServerIssuer, ResolvedClientIdentity identity) -> void
{
  const std::scoped_lock lock(mutex_);
  credentialsByIssuer_[std::move(authorizationServerIssuer)] = std::move(identity);
}

auto validateClientIdMetadataDocumentClientIdUrl(std::string_view clientIdUrl) -> void
{
  const ParsedUrl parsedClientIdUrl = validateAbsoluteUrl(clientIdUrl, "Client ID metadata document client_id", ClientRegistrationErrorCode::kInvalidInput);
  if (parsedClientIdUrl.scheme != "https")
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kInvalidInput, "Client ID metadata document client_id must use HTTPS");
  }

  if (parsedClientIdUrl.path.size() <= 1)
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kInvalidInput, "Client ID metadata document client_id URL must include a non-root path component");
  }
}

auto buildClientIdMetadataDocumentPayload(const ClientIdMetadataDocumentConfiguration &configuration) -> std::string
{
  validateClientIdMetadataDocumentClientIdUrl(configuration.clientId);

  const std::string clientName = requireTrimmedValue(configuration.clientName, "client_name", ClientRegistrationErrorCode::kInvalidInput);
  const std::vector<std::string> redirectUris = sanitizeStringList(configuration.redirectUris,
                                                                   "redirect_uris",
                                                                   ClientRegistrationErrorCode::kInvalidInput,
                                                                   /*requireNonEmpty=*/true);

  if (methodRequiresClientSecret(configuration.authenticationMethod))
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kInvalidInput,
                                  "Client ID metadata document payload generation does not support client_secret authentication methods");
  }

  jsonrpc::JsonValue payload = jsonrpc::JsonValue::object();
  payload["client_id"] = std::string(trimAsciiWhitespace(configuration.clientId));
  payload["client_name"] = clientName;

  payload["redirect_uris"] = jsonrpc::JsonValue::array();
  for (const std::string &redirectUri : redirectUris)
  {
    payload["redirect_uris"].push_back(redirectUri);
  }

  payload["grant_types"] = jsonrpc::JsonValue::array();
  payload["grant_types"].push_back("authorization_code");

  payload["response_types"] = jsonrpc::JsonValue::array();
  payload["response_types"].push_back("code");

  payload["token_endpoint_auth_method"] = toTokenEndpointAuthMethod(configuration.authenticationMethod);

  if (configuration.clientUri.has_value())
  {
    const std::string_view trimmedClientUri = trimAsciiWhitespace(*configuration.clientUri);
    if (!trimmedClientUri.empty())
    {
      payload["client_uri"] = std::string(trimmedClientUri);
    }
  }

  if (configuration.logoUri.has_value())
  {
    const std::string_view trimmedLogoUri = trimAsciiWhitespace(*configuration.logoUri);
    if (!trimmedLogoUri.empty())
    {
      payload["logo_uri"] = std::string(trimmedLogoUri);
    }
  }

  std::string encodedPayload;
  payload.dump(encodedPayload);
  return encodedPayload;
}

auto validateClientIdMetadataDocumentPayload(std::string_view metadataDocumentJson, std::string_view expectedClientIdUrl) -> void
{
  validateClientIdMetadataDocumentClientIdUrl(expectedClientIdUrl);
  const jsonrpc::JsonValue parsedPayload = parseJsonObject(metadataDocumentJson, "Client ID metadata document", ClientRegistrationErrorCode::kMetadataValidation);

  if (!parsedPayload.contains("client_id") || !parsedPayload["client_id"].is_string())
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kMetadataValidation, "Client ID metadata document.client_id must be a string");
  }

  const std::string documentClientId = parsedPayload["client_id"].as<std::string>();
  if (documentClientId != expectedClientIdUrl)
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kMetadataValidation, "Client ID metadata document.client_id must exactly match the hosted document URL");
  }

  validateClientIdMetadataDocumentClientIdUrl(documentClientId);

  if (!parsedPayload.contains("client_name") || !parsedPayload["client_name"].is_string())
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kMetadataValidation, "Client ID metadata document.client_name must be a non-empty string");
  }

  const std::string documentClientName = parsedPayload["client_name"].as<std::string>();
  if (trimAsciiWhitespace(documentClientName).empty())
  {
    throw ClientRegistrationError(ClientRegistrationErrorCode::kMetadataValidation, "Client ID metadata document.client_name must be a non-empty string");
  }

  static_cast<void>(parseOptionalStringArray(parsedPayload,
                                             "redirect_uris",
                                             ClientRegistrationErrorCode::kMetadataValidation,
                                             /*required=*/true,
                                             /*requireNonEmpty=*/true));

  if (const auto authMethod = parseOptionalStringField(parsedPayload, "token_endpoint_auth_method", ClientRegistrationErrorCode::kMetadataValidation); authMethod.has_value())
  {
    static_cast<void>(parseTokenEndpointAuthMethod(*authMethod, ClientRegistrationErrorCode::kMetadataValidation));
  }
}

auto resolveClientRegistration(const ResolveClientRegistrationRequest &request) -> ClientRegistrationResult
{
  if (request.strategyConfiguration.preRegistered.has_value())
  {
    return resolvePreRegistered(*request.strategyConfiguration.preRegistered);
  }

  if (request.authorizationServerMetadata.clientIdMetadataDocumentSupported && request.strategyConfiguration.clientIdMetadataDocument.has_value())
  {
    return resolveClientIdMetadataDocument(*request.strategyConfiguration.clientIdMetadataDocument);
  }

  if (request.strategyConfiguration.dynamic.enabled && request.authorizationServerMetadata.registrationEndpoint.has_value())
  {
    return resolveDynamic(request);
  }

  throw ClientRegistrationError(ClientRegistrationErrorCode::kHostInteractionRequired,
                                "No feasible client registration strategy was available. Provide pre-registered client information, configure an HTTPS client_id metadata document "
                                "URL, or enable dynamic registration when the authorization server exposes registration_endpoint.");
}

}  // namespace mcp::auth

// NOLINTEND(llvm-prefer-static-over-anonymous-namespace, readability-static-definition-in-anonymous-namespace)
