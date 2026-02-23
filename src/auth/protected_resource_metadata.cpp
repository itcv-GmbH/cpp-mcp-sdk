#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "mcp/auth/authorization_discovery_error.hpp"
#include "mcp/auth/authorization_discovery_request.hpp"
#include "mcp/auth/authorization_discovery_result.hpp"
#include "mcp/auth/authorization_server_metadata.hpp"
#include "mcp/auth/bearer_www_authenticate_challenge.hpp"
#include "mcp/auth/bearer_www_authenticate_parameter.hpp"
#include "mcp/auth/discovery_header.hpp"
#include "mcp/auth/discovery_http_request.hpp"
#include "mcp/auth/discovery_http_response.hpp"
#include "mcp/auth/discovery_http_types.hpp"
#include "mcp/auth/discovery_security_policy.hpp"
#include "mcp/auth/oauth_scope_set.hpp"
#include "mcp/auth/protected_resource_metadata_data.hpp"
#include "mcp/detail/ascii.hpp"
#include "mcp/detail/url.hpp"
#include "mcp/jsonrpc/types.hpp"
#include "mcp/transport/http/header.hpp"
#include "mcp/transport/http/http_client_options.hpp"
#include "mcp/transport/http/http_client_runtime.hpp"
#include "mcp/transport/http/server_request.hpp"

// NOLINTBEGIN(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, bugprone-argument-comment, misc-const-correctness, performance-unnecessary-value-param, performance-no-automatic-move,
// hicpp-move-const-arg, performance-move-const-arg, misc-include-cleaner, bugprone-unchecked-optional-access)

namespace mcp::auth
{
namespace
{

using transport::http::ServerRequestMethod;

constexpr std::uint16_t kHttpStatusOk = 200;
constexpr std::uint16_t kHttpStatusMovedPermanently = 301;
constexpr std::uint16_t kHttpStatusFound = 302;
constexpr std::uint16_t kHttpStatusSeeOther = 303;
constexpr std::uint16_t kHttpStatusTemporaryRedirect = 307;
constexpr std::uint16_t kHttpStatusPermanentRedirect = 308;
constexpr std::uint16_t kHttpStatusNotFound = 404;
constexpr std::uint32_t kIpv4FirstOctetMask = 0xFF000000U;
constexpr std::uint32_t kIpv4SecondOctetMask = 0x00FF0000U;
constexpr std::uint32_t kIpv4ThirdOctetMask = 0x0000FF00U;
constexpr std::uint32_t kIpv4SecondOctetShift = 16U;
constexpr std::uint32_t kIpv4ThirdOctetShift = 8U;

struct ParsedUrl
{
  std::string scheme;
  std::string host;
  std::string port;
  std::string path;
  std::optional<std::string> query;
  bool hasExplicitPort = false;
  bool ipv6Literal = false;
};

auto startsWithIgnoreCase(std::string_view value, std::string_view prefix) -> bool
{
  return value.size() >= prefix.size() && mcp::detail::equalsIgnoreCaseAscii(value.substr(0, prefix.size()), prefix);
}

auto endsWithIgnoreCase(std::string_view value, std::string_view suffix) -> bool
{
  return value.size() >= suffix.size() && mcp::detail::equalsIgnoreCaseAscii(value.substr(value.size() - suffix.size()), suffix);
}

auto isTokenCharacter(char character) -> bool
{
  const auto byte = static_cast<unsigned char>(character);
  return std::isalnum(byte) != 0 || character == '!' || character == '#' || character == '$' || character == '%' || character == '&' || character == '\'' || character == '*'
    || character == '+' || character == '-' || character == '.' || character == '^' || character == '_' || character == '`' || character == '|' || character == '~';
}

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

auto parseAbsoluteUrl(std::string_view rawUrl, bool allowQuery) -> ParsedUrl
{
  const std::string_view trimmed = mcp::detail::trimAsciiWhitespace(rawUrl);
  if (trimmed.empty())
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kInvalidInput, "URL must not be empty");
  }

  if (mcp::detail::containsAsciiWhitespaceOrControl(trimmed))
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kInvalidInput, "URL must not include whitespace or control characters");
  }

  if (trimmed.find('#') != std::string_view::npos)
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kInvalidInput, "URL fragments are not allowed");
  }

  const auto parsedAbsolute = mcp::detail::parseAbsoluteUrl(trimmed);
  if (!parsedAbsolute.has_value())
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kInvalidInput, "URL must be absolute and include a scheme");
  }

  ParsedUrl parsed;
  parsed.scheme = parsedAbsolute->scheme;
  parsed.host = parsedAbsolute->host;
  parsed.ipv6Literal = parsedAbsolute->ipv6Literal;
  parsed.hasExplicitPort = parsedAbsolute->hasExplicitPort;
  parsed.path = parsedAbsolute->path.empty() ? "/" : parsedAbsolute->path;
  parsed.query = parsedAbsolute->query;

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
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kInvalidInput, "URL port is not valid for this scheme");
  }

  if (!allowQuery && parsedAbsolute->hasQuery)
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kInvalidInput, "URL query components are not allowed");
  }

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

auto normalizeEndpointPath(std::string path) -> std::string
{
  if (path.empty())
  {
    path = "/";
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

auto canonicalResourceIdentifier(const ParsedUrl &url) -> std::string
{
  std::string canonical = originForUrl(url);
  const std::string normalizedPath = normalizeEndpointPath(url.path);
  if (normalizedPath != "/")
  {
    canonical += normalizedPath;
  }

  if (url.query.has_value())
  {
    canonical.push_back('?');
    canonical += *url.query;
  }

  return canonical;
}

auto canonicalIssuerIdentifier(const ParsedUrl &url) -> std::string
{
  std::string canonical = originForUrl(url);
  const std::string normalizedPath = normalizeEndpointPath(url.path);
  if (normalizedPath != "/")
  {
    canonical += normalizedPath;
  }

  return canonical;
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

auto splitOnCommasOutsideQuotes(std::string_view value) -> std::vector<std::string>
{
  std::vector<std::string> parts;
  bool inQuotes = false;
  bool escaped = false;
  std::size_t partBegin = 0;

  for (std::size_t index = 0; index < value.size(); ++index)
  {
    const char character = value[index];
    if (character == '"' && !escaped)
    {
      inQuotes = !inQuotes;
    }

    if (character == ',' && !inQuotes)
    {
      parts.emplace_back(value.substr(partBegin, index - partBegin));
      partBegin = index + 1;
      escaped = false;
      continue;
    }

    escaped = inQuotes && character == '\\' && !escaped;
    if (character != '\\')
    {
      escaped = false;
    }
  }

  parts.emplace_back(value.substr(partBegin));
  return parts;
}

auto parseTokenPrefix(std::string_view value) -> std::pair<std::string_view, std::string_view>
{
  std::size_t tokenEnd = 0;
  while (tokenEnd < value.size() && isTokenCharacter(value[tokenEnd]))
  {
    ++tokenEnd;
  }

  if (tokenEnd == 0)
  {
    return {std::string_view {}, value};
  }

  return {value.substr(0, tokenEnd), value.substr(tokenEnd)};
}

auto parseQuotedValue(std::string_view value) -> std::optional<std::string>
{
  if (value.empty() || value.front() != '"')
  {
    return std::nullopt;
  }

  std::string decoded;
  decoded.reserve(value.size());
  bool escaped = false;

  for (std::size_t index = 1; index < value.size(); ++index)
  {
    const char character = value[index];
    if (escaped)
    {
      decoded.push_back(character);
      escaped = false;
      continue;
    }

    if (character == '\\')
    {
      escaped = true;
      continue;
    }

    if (character == '"')
    {
      const std::string_view remainder = mcp::detail::trimAsciiWhitespace(value.substr(index + 1));
      if (!remainder.empty())
      {
        return std::nullopt;
      }

      return decoded;
    }

    decoded.push_back(character);
  }

  return std::nullopt;
}

auto parseBearerParameter(std::string_view parameterValue, BearerWwwAuthenticateChallenge &challenge) -> void
{
  const std::string_view trimmedParameter = mcp::detail::trimAsciiWhitespace(parameterValue);
  if (trimmedParameter.empty())
  {
    return;
  }

  const std::size_t separator = trimmedParameter.find('=');
  if (separator == std::string_view::npos)
  {
    return;
  }

  const std::string_view nameView = mcp::detail::trimAsciiWhitespace(trimmedParameter.substr(0, separator));
  const std::string_view encodedValue = mcp::detail::trimAsciiWhitespace(trimmedParameter.substr(separator + 1));
  if (nameView.empty() || encodedValue.empty())
  {
    return;
  }

  const std::string normalizedName = mcp::detail::toLowerAscii(nameView);
  std::string value;
  if (encodedValue.front() == '"')
  {
    const auto decoded = parseQuotedValue(encodedValue);
    if (!decoded.has_value())
    {
      return;
    }

    value = *decoded;
  }
  else
  {
    value = std::string(encodedValue);
  }

  challenge.parameters.push_back(BearerWwwAuthenticateParameter {normalizedName, value});
  if (normalizedName == "resource_metadata")
  {
    challenge.resourceMetadata = value;
  }
  else if (normalizedName == "scope")
  {
    challenge.scope = value;
  }
  else if (normalizedName == "error")
  {
    challenge.error = value;
  }
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

    const std::string scope = std::string(scopeText.substr(begin, end - begin));
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

auto parseJsonObject(std::string_view document, std::string_view context) -> jsonrpc::JsonValue
{
  try
  {
    jsonrpc::JsonValue parsed = jsonrpc::JsonValue::parse(std::string(document));
    if (!parsed.is_object())
    {
      throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation, std::string(context) + " must be a JSON object");
    }

    return parsed;
  }
  catch (const AuthorizationDiscoveryError &)
  {
    throw;
  }
  catch (const std::exception &error)
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation, std::string("Failed to parse ") + std::string(context) + ": " + error.what());
  }
}

auto headerValue(const std::vector<DiscoveryHeader> &headers, std::string_view name) -> std::optional<std::string>
{
  for (const DiscoveryHeader &header : headers)
  {
    if (mcp::detail::equalsIgnoreCaseAscii(mcp::detail::trimAsciiWhitespace(header.name), name))
    {
      return header.value;
    }
  }

  return std::nullopt;
}

auto resolveRedirectUrl(const ParsedUrl &currentUrl, std::string_view locationHeader) -> std::string
{
  const std::string_view trimmedLocation = mcp::detail::trimAsciiWhitespace(locationHeader);
  if (trimmedLocation.empty())
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kNetworkFailure, "Redirect response is missing a non-empty Location header");
  }

  if (trimmedLocation.find("://") != std::string_view::npos)
  {
    return std::string(trimmedLocation);
  }

  const std::string origin = originForUrl(currentUrl);
  if (startsWithIgnoreCase(trimmedLocation, "//"))
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

auto isBlockedIpv4(std::uint32_t value) -> bool
{
  const auto octet0 = static_cast<std::uint32_t>((value & kIpv4FirstOctetMask) >> 24U);
  const auto octet1 = static_cast<std::uint32_t>((value & kIpv4SecondOctetMask) >> kIpv4SecondOctetShift);
  const auto octet2 = static_cast<std::uint32_t>((value & kIpv4ThirdOctetMask) >> kIpv4ThirdOctetShift);

  if (octet0 == 0U || octet0 == 10U || octet0 == 127U)
  {
    return true;
  }

  if (octet0 == 100U && octet1 >= 64U && octet1 <= 127U)
  {
    return true;
  }

  if (octet0 == 169U && octet1 == 254U)
  {
    return true;
  }

  if (octet0 == 172U && octet1 >= 16U && octet1 <= 31U)
  {
    return true;
  }

  if (octet0 == 192U && octet1 == 168U)
  {
    return true;
  }

  if (octet0 == 192U && octet1 == 0U && (octet2 == 0U || octet2 == 2U))
  {
    return true;
  }

  if (octet0 == 198U && (octet1 == 18U || octet1 == 19U || (octet1 == 51U && octet2 == 100U)))
  {
    return true;
  }

  if (octet0 == 203U && octet1 == 0U && octet2 == 113U)
  {
    return true;
  }

  return octet0 >= 224U;
}

auto isBlockedIpv6(const boost::asio::ip::address_v6 &address) -> bool
{
  if (address.is_loopback() || address.is_unspecified() || address.is_multicast())
  {
    return true;
  }

  if (address.is_v4_mapped())
  {
    const auto bytes = address.to_bytes();
    const std::uint32_t mappedAddress = (static_cast<std::uint32_t>(bytes[12]) << 24U) | (static_cast<std::uint32_t>(bytes[13]) << 16U)
      | (static_cast<std::uint32_t>(bytes[14]) << 8U) | static_cast<std::uint32_t>(bytes[15]);
    return isBlockedIpv4(mappedAddress);
  }

  const auto bytes = address.to_bytes();
  if ((bytes[0] & 0xFEU) == 0xFCU)
  {
    return true;
  }

  if (bytes[0] == 0xFEU && (bytes[1] & 0xC0U) == 0x80U)
  {
    return true;
  }

  return bytes[0] == 0x20U && bytes[1] == 0x01U && bytes[2] == 0x0DU && bytes[3] == 0xB8U;
}

auto isBlockedAddress(const boost::asio::ip::address &address) -> bool
{
  if (address.is_v4())
  {
    return isBlockedIpv4(address.to_v4().to_uint());
  }

  return isBlockedIpv6(address.to_v6());
}

auto resolveHostWithSystemDns(std::string_view host) -> std::vector<std::string>
{
  boost::asio::io_context ioContext;
  boost::asio::ip::tcp::resolver resolver(ioContext);

  boost::system::error_code error;
  const auto endpoints = resolver.resolve(std::string(host), std::string {}, error);
  if (error)
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kNetworkFailure, "Failed to resolve discovery host '" + std::string(host) + "': " + error.message());
  }

  std::vector<std::string> addresses;
  for (const auto &endpoint : endpoints)
  {
    addresses.push_back(endpoint.endpoint().address().to_string());
  }

  std::sort(addresses.begin(), addresses.end());
  addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());
  return addresses;
}

auto validateHostAddressability(const ParsedUrl &url, const DiscoveryDnsResolver &resolver, const DiscoverySecurityPolicy &policy) -> void
{
  if (policy.allowPrivateAndLocalAddresses)
  {
    return;
  }

  if (url.host == "localhost" || endsWithIgnoreCase(url.host, ".localhost"))
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation, "Discovery host resolves to localhost");
  }

  boost::system::error_code addressError;
  const auto parsedAddress = boost::asio::ip::make_address(url.host, addressError);
  if (!addressError)
  {
    if (isBlockedAddress(parsedAddress))
    {
      throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation, "Discovery URL resolves to a blocked private or local address");
    }

    return;
  }

  const auto resolvedAddresses = resolver(url.host);
  if (resolvedAddresses.empty())
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kNetworkFailure, "Discovery DNS resolution produced no addresses");
  }

  for (const std::string &resolvedAddressText : resolvedAddresses)
  {
    boost::system::error_code resolvedAddressError;
    const auto resolvedAddress = boost::asio::ip::make_address(resolvedAddressText, resolvedAddressError);
    if (resolvedAddressError)
    {
      throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kNetworkFailure, "Discovery DNS resolver returned an invalid address value");
    }

    if (isBlockedAddress(resolvedAddress))
    {
      throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation, "Discovery DNS resolver returned a blocked private or local address");
    }
  }
}

auto defaultHttpFetcher(const DiscoveryHttpRequest &request) -> DiscoveryHttpResponse
{
  if (!mcp::detail::equalsIgnoreCaseAscii(mcp::detail::trimAsciiWhitespace(request.method), "GET"))
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kInvalidInput, "Discovery HTTP fetcher only supports GET requests");
  }

  transport::http::HttpClientOptions options;
  options.endpointUrl = request.url;
  options.tls.verifyPeer = true;

  transport::http::HttpClientRuntime runtime(std::move(options));

  transport::http::ServerRequest runtimeRequest;
  runtimeRequest.method = ServerRequestMethod::kGet;
  runtimeRequest.path.clear();
  for (const DiscoveryHeader &header : request.headers)
  {
    runtimeRequest.headers.push_back(transport::http::Header {header.name, header.value});
  }

  const transport::http::ServerResponse runtimeResponse = runtime.execute(runtimeRequest);

  DiscoveryHttpResponse response;
  response.statusCode = runtimeResponse.statusCode;
  response.body = runtimeResponse.body;
  response.headers.reserve(runtimeResponse.headers.size());
  for (const transport::http::Header &header : runtimeResponse.headers)
  {
    response.headers.push_back(DiscoveryHeader {header.name, header.value});
  }

  return response;
}

auto executeDiscoveryGet(std::string initialUrl, const DiscoveryHttpFetcher &fetcher, const DiscoveryDnsResolver &resolver, const DiscoverySecurityPolicy &policy)
  -> DiscoveryHttpResponse
{
  std::size_t redirects = 0;
  ParsedUrl currentUrl = parseAbsoluteUrl(initialUrl, true);

  while (true)
  {
    if (policy.requireHttps && currentUrl.scheme != "https")
    {
      throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation, "Discovery URLs must use HTTPS");
    }

    validateHostAddressability(currentUrl, resolver, policy);

    DiscoveryHttpRequest request;
    request.method = "GET";
    request.url = serializeUrl(currentUrl);
    const DiscoveryHttpResponse response = fetcher(request);

    if (!isRedirectStatusCode(response.statusCode))
    {
      return response;
    }

    if (redirects >= policy.maxRedirects)
    {
      throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation, "Discovery redirect limit exceeded");
    }

    const auto redirectLocation = headerValue(response.headers, "Location");
    if (!redirectLocation.has_value())
    {
      throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kNetworkFailure, "Discovery redirect response did not include a Location header");
    }

    ParsedUrl redirectedUrl = parseAbsoluteUrl(resolveRedirectUrl(currentUrl, *redirectLocation), true);
    if (policy.requireHttps && redirectedUrl.scheme != "https")
    {
      throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation, "Discovery redirect downgraded away from HTTPS");
    }

    if (policy.requireSameOriginRedirects && originForUrl(redirectedUrl) != originForUrl(currentUrl))
    {
      throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation, "Discovery redirect changed origin");
    }

    currentUrl = std::move(redirectedUrl);
    ++redirects;
  }
}

auto buildPathBasedProtectedResourceMetadataUrl(const ParsedUrl &endpointUrl) -> std::string
{
  const std::string origin = originForUrl(endpointUrl);
  const std::string endpointPath = normalizeEndpointPath(endpointUrl.path);
  if (endpointPath == "/")
  {
    return origin + "/.well-known/oauth-protected-resource";
  }

  return origin + "/.well-known/oauth-protected-resource" + endpointPath;
}

auto buildRootProtectedResourceMetadataUrl(const ParsedUrl &endpointUrl) -> std::string
{
  return originForUrl(endpointUrl) + "/.well-known/oauth-protected-resource";
}

auto firstResourceMetadataChallenge(const std::vector<BearerWwwAuthenticateChallenge> &bearerChallenges) -> std::optional<BearerWwwAuthenticateChallenge>
{
  for (const auto &challenge : bearerChallenges)
  {
    if (challenge.resourceMetadata.has_value() && !mcp::detail::trimAsciiWhitespace(*challenge.resourceMetadata).empty())
    {
      return challenge;
    }
  }

  return std::nullopt;
}

auto selectAuthorizationServer(const ProtectedResourceMetadata &metadata) -> std::string
{
  for (const std::string &candidate : metadata.authorizationServers)
  {
    try
    {
      ParsedUrl issuer = parseAbsoluteUrl(candidate, false);
      if (issuer.scheme != "https")
      {
        continue;
      }

      return canonicalIssuerIdentifier(issuer);
    }
    catch (const AuthorizationDiscoveryError &)
    {
      continue;
    }
  }

  throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation, "Protected resource metadata did not contain a usable HTTPS authorization server issuer");
}

auto buildAuthorizationServerMetadataCandidateUrls(std::string_view issuer) -> std::vector<std::string>
{
  ParsedUrl issuerUrl = parseAbsoluteUrl(issuer, false);
  const std::string origin = originForUrl(issuerUrl);
  const std::string normalizedPath = normalizeEndpointPath(issuerUrl.path);

  std::vector<std::string> candidates;
  if (normalizedPath == "/")
  {
    candidates.push_back(origin + "/.well-known/oauth-authorization-server");
    candidates.push_back(origin + "/.well-known/openid-configuration");
    return candidates;
  }

  candidates.push_back(origin + "/.well-known/oauth-authorization-server" + normalizedPath);
  candidates.push_back(origin + "/.well-known/openid-configuration" + normalizedPath);
  candidates.push_back(origin + normalizedPath + "/.well-known/openid-configuration");
  return candidates;
}

auto parseStringArray(const jsonrpc::JsonValue &value, std::string_view fieldName) -> std::vector<std::string>
{
  if (!value.is_array())
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation, std::string(fieldName) + " must be an array");
  }

  std::vector<std::string> parsedValues;
  for (const auto &item : value.array_range())
  {
    if (!item.is_string())
    {
      throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation, std::string(fieldName) + " entries must be strings");
    }

    const std::string itemValue = item.as<std::string>();
    const std::string_view trimmedItem = mcp::detail::trimAsciiWhitespace(itemValue);
    if (trimmedItem.empty())
    {
      continue;
    }

    if (std::find(parsedValues.begin(), parsedValues.end(), trimmedItem) == parsedValues.end())
    {
      parsedValues.emplace_back(trimmedItem);
    }
  }

  return parsedValues;
}

auto parseOptionalString(const jsonrpc::JsonValue &value, std::string_view fieldName) -> std::optional<std::string>
{
  if (!value.is_object() || !value.contains(std::string(fieldName)))
  {
    return std::nullopt;
  }

  const jsonrpc::JsonValue &fieldValue = value[std::string(fieldName)];
  if (!fieldValue.is_string())
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation, std::string(fieldName) + " must be a string when present");
  }

  const std::string fieldText = fieldValue.as<std::string>();
  const std::string_view trimmed = mcp::detail::trimAsciiWhitespace(fieldText);
  if (trimmed.empty())
  {
    return std::nullopt;
  }

  return std::string(trimmed);
}

auto resolveDnsResolver(const DiscoveryDnsResolver &resolver) -> DiscoveryDnsResolver
{
  if (resolver)
  {
    return resolver;
  }

  return [](std::string_view host) -> std::vector<std::string> { return resolveHostWithSystemDns(host); };
}

auto resolveHttpFetcher(const DiscoveryHttpFetcher &fetcher) -> DiscoveryHttpFetcher
{
  if (fetcher)
  {
    return fetcher;
  }

  return defaultHttpFetcher;
}

}  // namespace

AuthorizationDiscoveryError::AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode code, std::string message)
  : std::runtime_error(std::move(message))
  , code_(code)
{
}

auto AuthorizationDiscoveryError::code() const noexcept -> AuthorizationDiscoveryErrorCode
{
  return code_;
}

auto parseBearerWwwAuthenticateChallenges(const std::vector<std::string> &headerValues) -> std::vector<BearerWwwAuthenticateChallenge>
{
  std::vector<BearerWwwAuthenticateChallenge> bearerChallenges;

  for (const std::string &headerValue : headerValues)
  {
    const auto parts = splitOnCommasOutsideQuotes(headerValue);
    std::optional<std::size_t> activeBearerChallengeIndex;

    for (const std::string &part : parts)
    {
      const std::string_view trimmedPart = mcp::detail::trimAsciiWhitespace(part);
      if (trimmedPart.empty())
      {
        continue;
      }

      const auto [token, tokenRemainder] = parseTokenPrefix(trimmedPart);
      if (token.empty())
      {
        continue;
      }

      const std::string_view trimmedRemainder = mcp::detail::trimAsciiWhitespace(tokenRemainder);
      const bool schemeSegment = !trimmedRemainder.empty() && trimmedRemainder.front() != '=';
      if (schemeSegment)
      {
        if (!mcp::detail::equalsIgnoreCaseAscii(token, "Bearer"))
        {
          activeBearerChallengeIndex.reset();
          continue;
        }

        bearerChallenges.emplace_back();
        activeBearerChallengeIndex = bearerChallenges.size() - 1;
        parseBearerParameter(trimmedRemainder, bearerChallenges[*activeBearerChallengeIndex]);
        continue;
      }

      if (activeBearerChallengeIndex.has_value())
      {
        parseBearerParameter(trimmedPart, bearerChallenges[*activeBearerChallengeIndex]);
      }
    }
  }

  return bearerChallenges;
}

auto parseProtectedResourceMetadata(std::string_view jsonDocument) -> ProtectedResourceMetadata
{
  const jsonrpc::JsonValue parsedMetadata = parseJsonObject(jsonDocument, "Protected resource metadata");

  if (!parsedMetadata.contains("resource") || !parsedMetadata["resource"].is_string())
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation, "Protected resource metadata.resource must be a string");
  }

  const std::string resource = std::string(mcp::detail::trimAsciiWhitespace(parsedMetadata["resource"].as<std::string>()));
  if (resource.empty())
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation, "Protected resource metadata.resource must not be empty");
  }

  const ParsedUrl resourceUrl = parseAbsoluteUrl(resource, true);
  if (resourceUrl.scheme != "https")
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation, "Protected resource metadata.resource must use HTTPS");
  }

  if (!parsedMetadata.contains("authorization_servers"))
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation,
                                      "Protected resource metadata.authorization_servers must be present for MCP authorization");
  }

  ProtectedResourceMetadata metadata;
  metadata.resource = canonicalResourceIdentifier(resourceUrl);
  metadata.authorizationServers = parseStringArray(parsedMetadata["authorization_servers"], "authorization_servers");
  if (metadata.authorizationServers.empty())
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation, "Protected resource metadata.authorization_servers must contain at least one issuer");
  }

  if (parsedMetadata.contains("scopes_supported"))
  {
    OAuthScopeSet scopes;
    scopes.values = parseStringArray(parsedMetadata["scopes_supported"], "scopes_supported");
    scopes = sanitizeScopeSet(scopes);
    if (!scopes.values.empty())
    {
      metadata.scopesSupported = scopes;
    }
  }

  return metadata;
}

auto parseAuthorizationServerMetadata(std::string_view jsonDocument) -> AuthorizationServerMetadata
{
  const jsonrpc::JsonValue parsedMetadata = parseJsonObject(jsonDocument, "Authorization server metadata");

  if (!parsedMetadata.contains("issuer") || !parsedMetadata["issuer"].is_string())
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation, "Authorization server metadata.issuer must be a string");
  }

  const std::string issuerValue = std::string(mcp::detail::trimAsciiWhitespace(parsedMetadata["issuer"].as<std::string>()));
  if (issuerValue.empty())
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation, "Authorization server metadata.issuer must not be empty");
  }

  ParsedUrl issuerUrl = parseAbsoluteUrl(issuerValue, false);
  if (issuerUrl.scheme != "https")
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation, "Authorization server metadata.issuer must use HTTPS");
  }

  AuthorizationServerMetadata metadata;
  metadata.issuer = canonicalIssuerIdentifier(issuerUrl);
  metadata.authorizationEndpoint = parseOptionalString(parsedMetadata, "authorization_endpoint");
  metadata.tokenEndpoint = parseOptionalString(parsedMetadata, "token_endpoint");
  metadata.registrationEndpoint = parseOptionalString(parsedMetadata, "registration_endpoint");

  if (parsedMetadata.contains("code_challenge_methods_supported"))
  {
    metadata.codeChallengeMethodsSupported = parseStringArray(parsedMetadata["code_challenge_methods_supported"], "code_challenge_methods_supported");
  }

  if (parsedMetadata.contains("client_id_metadata_document_supported"))
  {
    if (!parsedMetadata["client_id_metadata_document_supported"].is_bool())
    {
      throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation,
                                        "Authorization server metadata.client_id_metadata_document_supported must be a boolean when present");
    }

    metadata.clientIdMetadataDocumentSupported = parsedMetadata["client_id_metadata_document_supported"].as<bool>();
  }

  return metadata;
}

auto selectScopesForAuthorization(const std::vector<BearerWwwAuthenticateChallenge> &bearerChallenges, const ProtectedResourceMetadata &metadata) -> std::optional<OAuthScopeSet>
{
  for (const auto &challenge : bearerChallenges)
  {
    if (!challenge.scope.has_value())
    {
      continue;
    }

    const OAuthScopeSet challengeScopes = sanitizeScopeSet(parseScopeString(*challenge.scope));
    if (!challengeScopes.values.empty())
    {
      return challengeScopes;
    }
  }

  if (metadata.scopesSupported.has_value())
  {
    const OAuthScopeSet supportedScopes = sanitizeScopeSet(*metadata.scopesSupported);
    if (!supportedScopes.values.empty())
    {
      return supportedScopes;
    }
  }

  return std::nullopt;
}

auto discoverAuthorizationMetadata(const AuthorizationDiscoveryRequest &request) -> AuthorizationDiscoveryResult
{
  if (mcp::detail::trimAsciiWhitespace(request.mcpEndpointUrl).empty())
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kInvalidInput, "Authorization discovery requires a non-empty MCP endpoint URL");
  }

  const DiscoveryDnsResolver dnsResolver = resolveDnsResolver(request.dnsResolver);
  const DiscoveryHttpFetcher httpFetcher = resolveHttpFetcher(request.httpFetcher);

  const ParsedUrl endpointUrl = parseAbsoluteUrl(request.mcpEndpointUrl, true);
  if (request.securityPolicy.requireHttps && endpointUrl.scheme != "https")
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kSecurityPolicyViolation, "Authorization discovery requires an HTTPS MCP endpoint URL");
  }

  AuthorizationDiscoveryResult result;
  result.bearerChallenges = parseBearerWwwAuthenticateChallenges(request.wwwAuthenticateHeaderValues);
  result.selectedBearerChallenge = firstResourceMetadataChallenge(result.bearerChallenges);
  if (!result.selectedBearerChallenge.has_value() && !result.bearerChallenges.empty())
  {
    result.selectedBearerChallenge = result.bearerChallenges.front();
  }

  const std::string endpointResourceIdentifier = canonicalResourceIdentifier(endpointUrl);

  if (const auto challenge = firstResourceMetadataChallenge(result.bearerChallenges); challenge.has_value())
  {
    const std::string_view challengeUrl = mcp::detail::trimAsciiWhitespace(*challenge->resourceMetadata);
    if (challengeUrl.empty())
    {
      throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kInvalidInput, "WWW-Authenticate resource_metadata must not be empty");
    }

    const DiscoveryHttpResponse metadataResponse = executeDiscoveryGet(std::string(challengeUrl), httpFetcher, dnsResolver, request.securityPolicy);
    if (metadataResponse.statusCode != kHttpStatusOk)
    {
      throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kNotFound, "Protected resource metadata fetch failed using WWW-Authenticate resource_metadata URL");
    }

    result.protectedResourceMetadataUrl = std::string(challengeUrl);
    result.protectedResourceMetadata = parseProtectedResourceMetadata(metadataResponse.body);
  }
  else
  {
    const std::vector<std::string> metadataCandidates = {
      buildPathBasedProtectedResourceMetadataUrl(endpointUrl),
      buildRootProtectedResourceMetadataUrl(endpointUrl),
    };

    bool metadataResolved = false;
    for (const std::string &candidateUrl : metadataCandidates)
    {
      const DiscoveryHttpResponse metadataResponse = executeDiscoveryGet(candidateUrl, httpFetcher, dnsResolver, request.securityPolicy);
      if (metadataResponse.statusCode == kHttpStatusNotFound)
      {
        continue;
      }

      if (metadataResponse.statusCode != kHttpStatusOk)
      {
        throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kNetworkFailure, "Protected resource metadata fetch failed while probing RFC9728 well-known URLs");
      }

      result.protectedResourceMetadataUrl = candidateUrl;
      result.protectedResourceMetadata = parseProtectedResourceMetadata(metadataResponse.body);
      metadataResolved = true;
      break;
    }

    if (!metadataResolved)
    {
      throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kNotFound, "Protected resource metadata could not be discovered from RFC9728 well-known URLs");
    }
  }

  if (result.protectedResourceMetadata.resource != endpointResourceIdentifier)
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kMetadataValidation, "Protected resource metadata.resource does not match the MCP endpoint URL");
  }

  result.selectedAuthorizationServer = selectAuthorizationServer(result.protectedResourceMetadata);
  const std::vector<std::string> metadataEndpoints = buildAuthorizationServerMetadataCandidateUrls(result.selectedAuthorizationServer);

  bool authorizationMetadataResolved = false;
  for (const std::string &metadataEndpoint : metadataEndpoints)
  {
    const DiscoveryHttpResponse authorizationMetadataResponse = executeDiscoveryGet(metadataEndpoint, httpFetcher, dnsResolver, request.securityPolicy);
    if (authorizationMetadataResponse.statusCode == kHttpStatusNotFound)
    {
      continue;
    }

    if (authorizationMetadataResponse.statusCode != kHttpStatusOk)
    {
      continue;
    }

    AuthorizationServerMetadata parsedAuthorizationMetadata;
    try
    {
      parsedAuthorizationMetadata = parseAuthorizationServerMetadata(authorizationMetadataResponse.body);
    }
    catch (const AuthorizationDiscoveryError &)
    {
      continue;
    }

    if (parsedAuthorizationMetadata.issuer != result.selectedAuthorizationServer)
    {
      continue;
    }

    result.authorizationServerMetadataUrl = metadataEndpoint;
    result.authorizationServerMetadata = std::move(parsedAuthorizationMetadata);
    authorizationMetadataResolved = true;
    break;
  }

  if (!authorizationMetadataResolved)
  {
    throw AuthorizationDiscoveryError(AuthorizationDiscoveryErrorCode::kNotFound, "Authorization server metadata discovery failed for every RFC8414/OIDC candidate endpoint");
  }

  result.selectedScopes = selectScopesForAuthorization(result.bearerChallenges, result.protectedResourceMetadata);
  return result;
}

}  // namespace mcp::auth

// NOLINTEND(llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, bugprone-argument-comment, misc-const-correctness, performance-unnecessary-value-param, performance-no-automatic-move,
// hicpp-move-const-arg, performance-move-const-arg, misc-include-cleaner, bugprone-unchecked-optional-access)
