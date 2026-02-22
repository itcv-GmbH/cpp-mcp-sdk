#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <mcp/auth/oauth_client.hpp>
#include <mcp/detail/ascii.hpp>
#include <mcp/detail/base64url.hpp>
#include <mcp/detail/url.hpp>
#include <mcp/security/crypto_random.hpp>
#include <openssl/evp.h>
#include <openssl/sha.h>

// NOLINTBEGIN(llvm-prefer-static-over-anonymous-namespace, cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays,
// hicpp-signed-bitwise, modernize-return-braced-init-list, cppcoreguidelines-pro-bounds-pointer-arithmetic)

namespace mcp::auth
{
namespace
{

constexpr std::size_t kPkceVerifierMinLength = 43U;
constexpr std::size_t kPkceVerifierMaxLength = 128U;
constexpr std::string_view kPkceMethodS256 = "S256";
constexpr std::uint16_t kHttpStatusMovedPermanently = 301;
constexpr std::uint16_t kHttpStatusFound = 302;
constexpr std::uint16_t kHttpStatusSeeOther = 303;
constexpr std::uint16_t kHttpStatusTemporaryRedirect = 307;
constexpr std::uint16_t kHttpStatusPermanentRedirect = 308;

struct ParsedUrl
{
  std::string scheme;
  std::string host;
  std::string port;
  std::string path;
  std::optional<std::string> query;
  std::string serialized;
  bool hasExplicitPort = false;
  bool ipv6Literal = false;
  bool hasQuery = false;
};

auto extractExplicitPortText(std::string_view rawUrl, bool ipv6Literal) -> std::string
{
  const std::size_t schemeSeparator = rawUrl.find("://");
  if (schemeSeparator == std::string_view::npos)
  {
    return {};
  }

  const std::size_t authorityBegin = schemeSeparator + 3;
  std::size_t authorityEnd = rawUrl.find_first_of("/?#", authorityBegin);
  if (authorityEnd == std::string_view::npos)
  {
    authorityEnd = rawUrl.size();
  }

  const std::string_view authority = rawUrl.substr(authorityBegin, authorityEnd - authorityBegin);
  if (authority.empty())
  {
    return {};
  }

  if (ipv6Literal)
  {
    const std::size_t closingBracket = authority.find(']');
    if (closingBracket == std::string_view::npos || closingBracket + 1 >= authority.size() || authority[closingBracket + 1] != ':')
    {
      return {};
    }

    return std::string(authority.substr(closingBracket + 2));
  }

  const std::size_t separator = authority.rfind(':');
  if (separator == std::string_view::npos)
  {
    return {};
  }

  return std::string(authority.substr(separator + 1));
}

auto defaultPortForScheme(std::string_view scheme) -> std::string
{
  if (scheme == "https")
  {
    return "443";
  }

  if (scheme == "http")
  {
    return "80";
  }

  return {};
}

auto parseAbsoluteUrl(std::string_view rawUrl, std::string_view fieldName) -> ParsedUrl
{
  const std::string_view trimmed = mcp::detail::trimAsciiWhitespace(rawUrl);
  if (trimmed.empty())
  {
    throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, std::string(fieldName) + " must not be empty");
  }

  if (mcp::detail::containsAsciiWhitespaceOrControl(trimmed))
  {
    throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, std::string(fieldName) + " must not include whitespace or control characters");
  }

  if (trimmed.find('#') != std::string_view::npos)
  {
    throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, std::string(fieldName) + " must not include URL fragments");
  }

  const auto parsedAbsolute = mcp::detail::parseAbsoluteUrl(trimmed);
  if (!parsedAbsolute.has_value())
  {
    throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, std::string(fieldName) + " must be an absolute URL");
  }

  ParsedUrl parsed;
  parsed.scheme = parsedAbsolute->scheme;
  parsed.host = parsedAbsolute->host;
  parsed.ipv6Literal = parsedAbsolute->ipv6Literal;
  parsed.hasExplicitPort = parsedAbsolute->hasExplicitPort;
  parsed.path = parsedAbsolute->path.empty() ? "/" : parsedAbsolute->path;
  parsed.query = parsedAbsolute->query;
  parsed.hasQuery = parsedAbsolute->hasQuery;

  if (parsed.hasExplicitPort)
  {
    parsed.port = std::to_string(parsedAbsolute->port);
    const std::string explicitPort = extractExplicitPortText(trimmed, parsed.ipv6Literal);
    if (!explicitPort.empty())
    {
      parsed.port = explicitPort;
    }
  }
  else
  {
    parsed.port = defaultPortForScheme(parsed.scheme);
  }

  if (parsed.port.empty())
  {
    throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, std::string(fieldName) + " port is not valid for this scheme");
  }

  parsed.serialized = std::string(trimmed);
  return parsed;
}

auto originForUrl(const ParsedUrl &url) -> std::string
{
  std::string origin = url.scheme;
  origin += "://";
  if (url.ipv6Literal)
  {
    origin += "[";
    origin += url.host;
    origin += "]";
  }
  else
  {
    origin += url.host;
  }

  const std::string defaultPort = defaultPortForScheme(url.scheme);
  if (url.hasExplicitPort || url.port != defaultPort)
  {
    origin.push_back(':');
    origin += url.port;
  }

  return origin;
}

auto serializeUrl(const ParsedUrl &url) -> std::string
{
  std::string serialized = originForUrl(url);
  serialized += url.path.empty() ? "/" : url.path;
  if (url.query.has_value())
  {
    serialized.push_back('?');
    serialized += *url.query;
  }

  return serialized;
}

auto isRedirectStatusCode(std::uint16_t statusCode) -> bool
{
  return statusCode == kHttpStatusMovedPermanently || statusCode == kHttpStatusFound || statusCode == kHttpStatusSeeOther || statusCode == kHttpStatusTemporaryRedirect
    || statusCode == kHttpStatusPermanentRedirect;
}

auto resolveRedirectUrl(const ParsedUrl &currentUrl, std::string_view locationHeader) -> std::string
{
  const std::string_view trimmedLocation = mcp::detail::trimAsciiWhitespace(locationHeader);
  if (trimmedLocation.empty())
  {
    throw OAuthClientError(OAuthClientErrorCode::kNetworkFailure, "Redirect response is missing a non-empty Location header");
  }

  if (trimmedLocation.find("://") != std::string_view::npos)
  {
    return std::string(trimmedLocation);
  }

  const std::string origin = originForUrl(currentUrl);
  if (trimmedLocation.size() >= 2 && trimmedLocation[0] == '/' && trimmedLocation[1] == '/')
  {
    return currentUrl.scheme + ":" + std::string(trimmedLocation);
  }

  if (trimmedLocation.front() == '/')
  {
    return origin + std::string(trimmedLocation);
  }

  std::string basePath = currentUrl.path.empty() ? "/" : currentUrl.path;
  const std::size_t separator = basePath.rfind('/');
  if (separator == std::string::npos)
  {
    basePath = "/";
  }
  else
  {
    basePath = basePath.substr(0, separator + 1);
  }

  return origin + basePath + std::string(trimmedLocation);
}

auto headerValue(const std::vector<OAuthHttpHeader> &headers, std::string_view name) -> std::optional<std::string>
{
  for (const OAuthHttpHeader &header : headers)
  {
    if (mcp::detail::equalsIgnoreCaseAscii(mcp::detail::trimAsciiWhitespace(header.name), name))
    {
      return header.value;
    }
  }

  return std::nullopt;
}

auto methodAllowsBodyOnRedirectRewrite(std::string_view method) -> bool
{
  const std::string normalized = mcp::detail::toLowerAscii(mcp::detail::trimAsciiWhitespace(method));
  return normalized == "post" || normalized == "put" || normalized == "patch" || normalized == "delete";
}

auto isUnreservedUriCharacter(unsigned char value) -> bool
{
  return std::isalnum(value) != 0 || value == '-' || value == '.' || value == '_' || value == '~';
}

auto toHexByte(unsigned char value) -> std::array<char, 2>
{
  constexpr std::array<char, 16> kHexCharacters = {
    '0',
    '1',
    '2',
    '3',
    '4',
    '5',
    '6',
    '7',
    '8',
    '9',
    'A',
    'B',
    'C',
    'D',
    'E',
    'F',
  };

  return {
    kHexCharacters[(value >> 4U) & 0x0FU],
    kHexCharacters[value & 0x0FU],
  };
}

auto percentEncodeComponent(std::string_view value) -> std::string
{
  std::string encoded;
  encoded.reserve(value.size() * 3U);
  for (const unsigned char character : value)
  {
    if (isUnreservedUriCharacter(character))
    {
      encoded.push_back(static_cast<char>(character));
      continue;
    }

    const std::array<char, 2> hex = toHexByte(character);
    encoded.push_back('%');
    encoded.push_back(hex[0]);
    encoded.push_back(hex[1]);
  }

  return encoded;
}

auto isReservedAuthorizationQueryParameter(std::string_view parameterName) -> bool
{
  static const std::set<std::string, std::less<>> reservedParameters = {
    "client_id",
    "code_challenge",
    "code_challenge_method",
    "redirect_uri",
    "resource",
    "response_type",
    "scope",
    "state",
  };

  return reservedParameters.find(mcp::detail::toLowerAscii(parameterName)) != reservedParameters.end();
}

auto isReservedTokenBodyParameter(std::string_view parameterName) -> bool
{
  static const std::set<std::string, std::less<>> reservedParameters = {
    "client_id",
    "code",
    "code_verifier",
    "grant_type",
    "redirect_uri",
    "resource",
  };

  return reservedParameters.find(mcp::detail::toLowerAscii(parameterName)) != reservedParameters.end();
}

auto isSensitiveTokenParameter(std::string_view parameterName) -> bool
{
  static const std::set<std::string, std::less<>> blockedParameters = {
    "access_token",
    "id_token",
    "refresh_token",
    "token",
  };

  return blockedParameters.find(mcp::detail::toLowerAscii(parameterName)) != blockedParameters.end();
}

auto joinScopes(const OAuthScopeSet &scopes) -> std::optional<std::string>
{
  std::string joined;
  for (const std::string &scope : scopes.values)
  {
    const std::string_view trimmedScope = mcp::detail::trimAsciiWhitespace(scope);
    if (trimmedScope.empty())
    {
      continue;
    }

    if (!joined.empty())
    {
      joined.push_back(' ');
    }

    joined += trimmedScope;
  }

  if (joined.empty())
  {
    return std::nullopt;
  }

  return joined;
}

auto parseScopeString(std::string_view scopeText) -> OAuthScopeSet
{
  OAuthScopeSet scopeSet;
  std::size_t begin = 0;

  while (begin < scopeText.size())
  {
    while (begin < scopeText.size() && std::isspace(static_cast<unsigned char>(scopeText[begin])) != 0)
    {
      ++begin;
    }

    if (begin >= scopeText.size())
    {
      break;
    }

    std::size_t end = begin;
    while (end < scopeText.size() && std::isspace(static_cast<unsigned char>(scopeText[end])) == 0)
    {
      ++end;
    }

    const std::string scope(scopeText.substr(begin, end - begin));
    if (!scope.empty() && std::find(scopeSet.values.begin(), scopeSet.values.end(), scope) == scopeSet.values.end())
    {
      scopeSet.values.push_back(scope);
    }

    begin = end;
  }

  return scopeSet;
}

auto sanitizeScopeSet(const OAuthScopeSet &scopes) -> OAuthScopeSet
{
  OAuthScopeSet sanitized;
  for (const std::string &scope : scopes.values)
  {
    const std::string_view trimmedScope = mcp::detail::trimAsciiWhitespace(scope);
    if (trimmedScope.empty())
    {
      continue;
    }

    if (std::find(sanitized.values.begin(), sanitized.values.end(), trimmedScope) != sanitized.values.end())
    {
      continue;
    }

    sanitized.values.emplace_back(trimmedScope);
  }

  return sanitized;
}

auto mergeScopes(const OAuthScopeSet &baseScopes, const OAuthScopeSet &requiredScopes) -> OAuthScopeSet
{
  OAuthScopeSet merged = sanitizeScopeSet(baseScopes);
  const OAuthScopeSet required = sanitizeScopeSet(requiredScopes);
  for (const std::string &scope : required.values)
  {
    if (std::find(merged.values.begin(), merged.values.end(), scope) == merged.values.end())
    {
      merged.values.push_back(scope);
    }
  }

  return merged;
}

auto canonicalScopeSetKey(const OAuthScopeSet &scopes) -> std::string
{
  OAuthScopeSet sanitized = sanitizeScopeSet(scopes);
  std::sort(sanitized.values.begin(), sanitized.values.end());

  std::string key;
  for (const std::string &scope : sanitized.values)
  {
    if (!key.empty())
    {
      key.push_back(' ');
    }

    key += scope;
  }

  return key;
}

auto normalizeResourceKey(std::string_view value) -> std::string
{
  const std::string_view trimmed = mcp::detail::trimAsciiWhitespace(value);
  if (trimmed.empty())
  {
    return {};
  }

  try
  {
    const ParsedUrl parsed = parseAbsoluteUrl(trimmed, "resource");
    return serializeUrl(parsed);
  }
  catch (const OAuthClientError &)
  {
    return std::string(trimmed);
  }
}

auto collectHeaderValues(const std::vector<OAuthHttpHeader> &headers, std::string_view name) -> std::vector<std::string>
{
  std::vector<std::string> values;
  for (const OAuthHttpHeader &header : headers)
  {
    if (mcp::detail::equalsIgnoreCaseAscii(mcp::detail::trimAsciiWhitespace(header.name), name))
    {
      values.push_back(header.value);
    }
  }

  return values;
}

auto withBearerAuthorization(const OAuthProtectedResourceRequest &request, const std::optional<OAuthAccessToken> &token) -> OAuthProtectedResourceRequest
{
  OAuthProtectedResourceRequest authorized = request;
  authorized.headers.erase(
    std::remove_if(authorized.headers.begin(),
                   authorized.headers.end(),
                   [](const OAuthHttpHeader &header) -> bool { return mcp::detail::equalsIgnoreCaseAscii(mcp::detail::trimAsciiWhitespace(header.name), "Authorization"); }),
    authorized.headers.end());

  if (token.has_value() && !token->value.empty())
  {
    authorized.headers.push_back(OAuthHttpHeader {"Authorization", "Bearer " + token->value});
  }

  return authorized;
}

auto toTokenRequest(const OAuthTokenHttpRequest &request) -> OAuthTokenHttpRequest
{
  OAuthTokenHttpRequest normalized = request;
  const std::string_view normalizedMethod = mcp::detail::trimAsciiWhitespace(normalized.method);
  normalized.method = normalizedMethod.empty() ? "POST" : std::string(normalizedMethod);
  return normalized;
}

auto requireNonEmptyTokenValue(std::string_view value, std::string_view fieldName) -> std::string
{
  const std::string_view trimmed = mcp::detail::trimAsciiWhitespace(value);
  if (trimmed.empty())
  {
    throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, std::string(fieldName) + " must not be empty");
  }

  return std::string(trimmed);
}

auto appendQueryParameter(std::string &url, bool &hasQuery, std::string_view name, std::string_view value) -> void
{
  url.push_back(hasQuery ? '&' : '?');
  hasQuery = true;
  url += percentEncodeComponent(name);
  url.push_back('=');
  url += percentEncodeComponent(value);
}

auto appendFormParameter(std::string &body, std::string_view name, std::string_view value) -> void
{
  if (!body.empty())
  {
    body.push_back('&');
  }

  body += percentEncodeComponent(name);
  body.push_back('=');
  body += percentEncodeComponent(value);
}

auto sha256Digest(std::string_view value) -> std::vector<std::uint8_t>
{
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest {};
  unsigned int digestLength = 0;

  if (EVP_Digest(value.data(), value.size(), digest.data(), &digestLength, EVP_sha256(), nullptr) != 1)
  {
    throw OAuthClientError(OAuthClientErrorCode::kCryptoFailure, "Failed to compute SHA-256 digest for PKCE challenge");
  }

  return std::vector<std::uint8_t>(digest.begin(), digest.begin() + digestLength);
}

}  // namespace

OAuthClientError::OAuthClientError(OAuthClientErrorCode code, const std::string &message)
  : std::runtime_error(message)
  , code_(code)
{
}

auto OAuthClientError::code() const noexcept -> OAuthClientErrorCode
{
  return code_;
}

auto validateAuthorizationServerPkceS256Support(const AuthorizationServerMetadata &metadata) -> void
{
  if (metadata.codeChallengeMethodsSupported.empty())
  {
    throw OAuthClientError(OAuthClientErrorCode::kMetadataValidation,
                           "Authorization server metadata must include code_challenge_methods_supported and advertise S256 for MCP OAuth");
  }

  const bool supportsS256 = std::any_of(metadata.codeChallengeMethodsSupported.begin(),
                                        metadata.codeChallengeMethodsSupported.end(),
                                        [](const std::string &method) -> bool { return mcp::detail::toLowerAscii(method) == mcp::detail::toLowerAscii(kPkceMethodS256); });

  if (!supportsS256)
  {
    throw OAuthClientError(OAuthClientErrorCode::kMetadataValidation, "Authorization server metadata does not support PKCE S256");
  }
}

auto validateRedirectUriForAuthorizationCodeFlow(std::string_view redirectUri) -> void
{
  const ParsedUrl parsed = parseAbsoluteUrl(redirectUri, "redirect_uri");
  if (parsed.scheme == "https")
  {
    return;
  }

  if (parsed.scheme == "http" && parsed.host == "localhost")
  {
    return;
  }

  throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, "redirect_uri must use HTTPS or be an http://localhost loopback URL");
}

auto generatePkceCodePair(std::size_t verifierEntropyBytes) -> PkceCodePair
{
  if (verifierEntropyBytes == 0)
  {
    throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, "verifierEntropyBytes must be greater than zero");
  }

  const std::vector<std::uint8_t> verifierEntropy = security::cryptoRandomBytes(verifierEntropyBytes);
  const std::string codeVerifier = mcp::detail::encodeBase64UrlNoPad(std::string_view(reinterpret_cast<const char *>(verifierEntropy.data()), verifierEntropy.size()));
  if (codeVerifier.size() < kPkceVerifierMinLength || codeVerifier.size() > kPkceVerifierMaxLength)
  {
    throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, "PKCE verifier length must be between 43 and 128 characters; adjust verifierEntropyBytes accordingly");
  }

  const std::vector<std::uint8_t> codeChallengeDigest = sha256Digest(codeVerifier);

  PkceCodePair pair;
  pair.codeVerifier = codeVerifier;
  pair.codeChallenge = mcp::detail::encodeBase64UrlNoPad(std::string_view(reinterpret_cast<const char *>(codeChallengeDigest.data()), codeChallengeDigest.size()));
  pair.codeChallengeMethod = std::string(kPkceMethodS256);
  return pair;
}

auto buildAuthorizationUrl(const OAuthAuthorizationUrlRequest &request) -> std::string
{
  validateAuthorizationServerPkceS256Support(request.authorizationServerMetadata);

  if (!request.authorizationServerMetadata.authorizationEndpoint.has_value())
  {
    throw OAuthClientError(OAuthClientErrorCode::kMetadataValidation, "Authorization server metadata does not include authorization_endpoint required for authorization code flow");
  }

  const ParsedUrl authorizationEndpoint = parseAbsoluteUrl(*request.authorizationServerMetadata.authorizationEndpoint, "authorization_endpoint");
  if (authorizationEndpoint.scheme != "https")
  {
    throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, "authorization_endpoint must use HTTPS");
  }

  validateRedirectUriForAuthorizationCodeFlow(request.redirectUri);

  const std::string clientId = requireNonEmptyTokenValue(request.clientId, "client_id");
  const std::string state = requireNonEmptyTokenValue(request.state, "state");
  const std::string codeChallenge = requireNonEmptyTokenValue(request.codeChallenge, "code_challenge");
  const std::string resource = requireNonEmptyTokenValue(request.resource, "resource");
  static_cast<void>(parseAbsoluteUrl(resource, "resource"));

  std::string url = authorizationEndpoint.serialized;
  bool hasQuery = authorizationEndpoint.hasQuery;
  appendQueryParameter(url, hasQuery, "response_type", "code");
  appendQueryParameter(url, hasQuery, "client_id", clientId);
  appendQueryParameter(url, hasQuery, "redirect_uri", request.redirectUri);
  appendQueryParameter(url, hasQuery, "state", state);
  appendQueryParameter(url, hasQuery, "code_challenge", codeChallenge);
  appendQueryParameter(url, hasQuery, "code_challenge_method", std::string(kPkceMethodS256));
  appendQueryParameter(url, hasQuery, "resource", resource);

  if (request.scopes.has_value())
  {
    const auto encodedScopes = joinScopes(*request.scopes);
    if (encodedScopes.has_value())
    {
      appendQueryParameter(url, hasQuery, "scope", *encodedScopes);
    }
  }

  for (const OAuthQueryParameter &parameter : request.additionalParameters)
  {
    const std::string name = requireNonEmptyTokenValue(parameter.name, "additional authorization parameter name");
    if (isReservedAuthorizationQueryParameter(name))
    {
      throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, "additional authorization parameter name conflicts with a reserved OAuth parameter: " + name);
    }

    if (isSensitiveTokenParameter(name))
    {
      throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, "Refusing to include token-like parameter in authorization URL query: " + name);
    }

    appendQueryParameter(url, hasQuery, name, parameter.value);
  }

  return url;
}

auto buildTokenExchangeHttpRequest(const OAuthTokenExchangeRequest &request) -> OAuthTokenHttpRequest
{
  validateAuthorizationServerPkceS256Support(request.authorizationServerMetadata);

  if (!request.authorizationServerMetadata.tokenEndpoint.has_value())
  {
    throw OAuthClientError(OAuthClientErrorCode::kMetadataValidation, "Authorization server metadata does not include token_endpoint required for code exchange");
  }

  const ParsedUrl tokenEndpoint = parseAbsoluteUrl(*request.authorizationServerMetadata.tokenEndpoint, "token_endpoint");
  if (tokenEndpoint.scheme != "https")
  {
    throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, "token_endpoint must use HTTPS");
  }

  validateRedirectUriForAuthorizationCodeFlow(request.redirectUri);

  const std::string clientId = requireNonEmptyTokenValue(request.clientId, "client_id");
  const std::string authorizationCode = requireNonEmptyTokenValue(request.authorizationCode, "authorization_code");
  const std::string codeVerifier = requireNonEmptyTokenValue(request.codeVerifier, "code_verifier");
  const std::string resource = requireNonEmptyTokenValue(request.resource, "resource");
  static_cast<void>(parseAbsoluteUrl(resource, "resource"));

  if (codeVerifier.size() < kPkceVerifierMinLength || codeVerifier.size() > kPkceVerifierMaxLength)
  {
    throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, "code_verifier length must be between 43 and 128 characters");
  }

  OAuthTokenHttpRequest tokenRequest;
  tokenRequest.method = "POST";
  tokenRequest.url = tokenEndpoint.serialized;
  tokenRequest.headers = {
    OAuthHttpHeader {"Content-Type", "application/x-www-form-urlencoded"},
    OAuthHttpHeader {"Accept", "application/json"},
  };

  appendFormParameter(tokenRequest.body, "grant_type", "authorization_code");
  appendFormParameter(tokenRequest.body, "code", authorizationCode);
  appendFormParameter(tokenRequest.body, "redirect_uri", request.redirectUri);
  appendFormParameter(tokenRequest.body, "client_id", clientId);
  appendFormParameter(tokenRequest.body, "code_verifier", codeVerifier);
  appendFormParameter(tokenRequest.body, "resource", resource);

  for (const OAuthQueryParameter &parameter : request.additionalParameters)
  {
    const std::string name = requireNonEmptyTokenValue(parameter.name, "additional token parameter name");
    if (isReservedTokenBodyParameter(name))
    {
      throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, "additional token parameter name conflicts with a reserved OAuth parameter: " + name);
    }

    if (isSensitiveTokenParameter(name))
    {
      throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, "Refusing to include token-like parameter in token request parameters: " + name);
    }

    appendFormParameter(tokenRequest.body, name, parameter.value);
  }

  return tokenRequest;
}

auto InMemoryOAuthTokenStorage::load(std::string_view resource) const -> std::optional<OAuthAccessToken>
{
  const std::string key = normalizeResourceKey(resource);
  if (key.empty())
  {
    return std::nullopt;
  }

  const std::scoped_lock lock(mutex_);
  const auto found = tokensByResource_.find(key);
  if (found == tokensByResource_.end())
  {
    return std::nullopt;
  }

  return found->second;
}

auto InMemoryOAuthTokenStorage::save(std::string resource, OAuthAccessToken token) -> void
{
  const std::string key = normalizeResourceKey(resource);
  if (key.empty())
  {
    throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, "Token storage resource key must not be empty");
  }

  const std::scoped_lock lock(mutex_);
  tokensByResource_[key] = std::move(token);
}

auto executeTokenRequestWithPolicy(const OAuthTokenRequestExecutionRequest &request) -> OAuthHttpResponse
{
  if (!request.requestExecutor)
  {
    throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, "Token request execution requires a non-null request executor");
  }

  OAuthTokenHttpRequest currentRequest = toTokenRequest(request.tokenRequest);
  ParsedUrl currentUrl = parseAbsoluteUrl(currentRequest.url, "token request URL");
  if (request.securityPolicy.requireHttps && currentUrl.scheme != "https")
  {
    throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, "Token endpoint interactions must use HTTPS");
  }

  std::size_t redirects = 0;
  while (true)
  {
    const OAuthHttpResponse response = request.requestExecutor(currentRequest);
    if (!isRedirectStatusCode(response.statusCode))
    {
      return response;
    }

    if (redirects >= request.securityPolicy.maxRedirects)
    {
      throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, "Token endpoint redirect limit exceeded");
    }

    const auto location = headerValue(response.headers, "Location");
    if (!location.has_value())
    {
      throw OAuthClientError(OAuthClientErrorCode::kNetworkFailure, "Token endpoint redirect response is missing a Location header");
    }

    const ParsedUrl redirectedUrl = parseAbsoluteUrl(resolveRedirectUrl(currentUrl, *location), "token endpoint redirect URL");
    if (request.securityPolicy.requireHttps && redirectedUrl.scheme != "https")
    {
      throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, "Token endpoint redirect downgraded away from HTTPS");
    }

    const bool sameOrigin = originForUrl(currentUrl) == originForUrl(redirectedUrl);
    if (request.securityPolicy.requireSameOriginRedirects && !sameOrigin)
    {
      throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, "Token endpoint redirect changed origin");
    }

    if (!sameOrigin)
    {
      currentRequest.headers.erase(
        std::remove_if(currentRequest.headers.begin(),
                       currentRequest.headers.end(),
                       [](const OAuthHttpHeader &header) -> bool { return mcp::detail::equalsIgnoreCaseAscii(mcp::detail::trimAsciiWhitespace(header.name), "Authorization"); }),
        currentRequest.headers.end());
    }

    if ((response.statusCode == kHttpStatusMovedPermanently || response.statusCode == kHttpStatusFound || response.statusCode == kHttpStatusSeeOther)
        && methodAllowsBodyOnRedirectRewrite(currentRequest.method))
    {
      throw OAuthClientError(OAuthClientErrorCode::kSecurityPolicyViolation, "Refusing to follow token endpoint redirects that can rewrite a credential-bearing request method");
    }

    currentRequest.url = serializeUrl(redirectedUrl);
    currentUrl = redirectedUrl;
    ++redirects;
  }
}

auto executeProtectedResourceRequestWithStepUp(const OAuthStepUpExecutionRequest &request) -> OAuthHttpResponse
{
  if (!request.requestExecutor)
  {
    throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, "Step-up execution requires a non-null request executor");
  }

  if (!request.authorizer)
  {
    throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, "Step-up execution requires a non-null authorizer callback");
  }

  std::shared_ptr<OAuthTokenStorage> tokenStorage = request.tokenStorage;
  if (!tokenStorage)
  {
    tokenStorage = std::make_shared<InMemoryOAuthTokenStorage>();
  }

  const std::string normalizedResource = normalizeResourceKey(request.resource);
  if (normalizedResource.empty())
  {
    throw OAuthClientError(OAuthClientErrorCode::kInvalidInput, "Step-up execution requires a non-empty resource identifier");
  }

  std::optional<OAuthAccessToken> currentToken = tokenStorage->load(normalizedResource);
  std::unordered_map<std::string, std::unordered_set<std::string>> attemptedScopeSetsByResource;
  std::size_t attempts = 0;

  OAuthHttpResponse response = request.requestExecutor(withBearerAuthorization(request.protectedResourceRequest, currentToken));
  while (response.statusCode == 403 && attempts < request.maxStepUpAttempts)
  {
    const std::vector<std::string> wwwAuthenticateHeaders = collectHeaderValues(response.headers, "WWW-Authenticate");
    if (wwwAuthenticateHeaders.empty())
    {
      break;
    }

    const std::vector<BearerWwwAuthenticateChallenge> challenges = parseBearerWwwAuthenticateChallenges(wwwAuthenticateHeaders);

    std::optional<BearerWwwAuthenticateChallenge> insufficientScopeChallenge;
    for (const BearerWwwAuthenticateChallenge &challenge : challenges)
    {
      if (challenge.error.has_value() && mcp::detail::equalsIgnoreCaseAscii(mcp::detail::trimAsciiWhitespace(*challenge.error), "insufficient_scope"))
      {
        insufficientScopeChallenge = challenge;
        break;
      }
    }

    if (!insufficientScopeChallenge.has_value() || !insufficientScopeChallenge->scope.has_value())
    {
      break;
    }

    const OAuthScopeSet requiredScopes = sanitizeScopeSet(parseScopeString(*insufficientScopeChallenge->scope));
    if (requiredScopes.values.empty())
    {
      break;
    }

    OAuthScopeSet baseScopes = request.initialScopes;
    if (currentToken.has_value())
    {
      baseScopes = mergeScopes(baseScopes, currentToken->scopes);
    }

    const OAuthScopeSet targetScopes = mergeScopes(baseScopes, requiredScopes);
    const std::string scopeSetKey = canonicalScopeSetKey(targetScopes);
    if (scopeSetKey.empty())
    {
      break;
    }

    std::optional<std::string> challengeResourceMetadata;
    std::string scopeTrackingResource = normalizedResource;
    if (insufficientScopeChallenge->resourceMetadata.has_value())
    {
      const std::string normalizedResourceMetadata = normalizeResourceKey(*insufficientScopeChallenge->resourceMetadata);
      if (!normalizedResourceMetadata.empty())
      {
        challengeResourceMetadata = normalizedResourceMetadata;
        scopeTrackingResource = normalizedResourceMetadata;
      }
    }

    auto &attemptedScopeSets = attemptedScopeSetsByResource[scopeTrackingResource];
    const auto insertResult = attemptedScopeSets.insert(scopeSetKey);
    if (!insertResult.second)
    {
      break;
    }

    OAuthStepUpAuthorizationRequest authorizationRequest;
    authorizationRequest.resource = normalizedResource;
    authorizationRequest.requestedScopes = targetScopes;
    authorizationRequest.resourceMetadataUrl = challengeResourceMetadata;

    OAuthAccessToken refreshedToken = request.authorizer(authorizationRequest);
    refreshedToken.value = requireNonEmptyTokenValue(refreshedToken.value, "step-up access token");
    refreshedToken.scopes = sanitizeScopeSet(refreshedToken.scopes);
    tokenStorage->save(normalizedResource, refreshedToken);

    currentToken = std::move(refreshedToken);
    ++attempts;
    response = request.requestExecutor(withBearerAuthorization(request.protectedResourceRequest, currentToken));
  }

  return response;
}

}  // namespace mcp::auth

// NOLINTEND(llvm-prefer-static-over-anonymous-namespace, cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays,
// hicpp-signed-bitwise, modernize-return-braced-init-list, cppcoreguidelines-pro-bounds-pointer-arithmetic)
