#include <catch2/catch_test_macros.hpp>
#include <mcp/detail/url.hpp>

// NOLINTBEGIN(readability-function-cognitive-complexity)

using mcp::detail::parseAbsoluteUrl;
using mcp::detail::ParsedAbsoluteUrl;

TEST_CASE("parseAbsoluteUrl rejects empty string", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("");
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseAbsoluteUrl rejects URLs without scheme", "[detail][url]")
{
  REQUIRE_FALSE(parseAbsoluteUrl("example.com").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("//example.com").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("/path/to/resource").has_value());
}

TEST_CASE("parseAbsoluteUrl rejects URLs without ://", "[detail][url]")
{
  REQUIRE_FALSE(parseAbsoluteUrl("http:example.com").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("https:/example.com").has_value());
}

TEST_CASE("parseAbsoluteUrl rejects empty scheme", "[detail][url]")
{
  REQUIRE_FALSE(parseAbsoluteUrl("://example.com").has_value());
}

TEST_CASE("parseAbsoluteUrl rejects empty authority", "[detail][url]")
{
  REQUIRE_FALSE(parseAbsoluteUrl("http://").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("https:///path").has_value());
}

TEST_CASE("parseAbsoluteUrl rejects empty host", "[detail][url]")
{
  REQUIRE_FALSE(parseAbsoluteUrl("http://:8080").has_value());
}

TEST_CASE("parseAbsoluteUrl parses basic http URL", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("http://example.com");
  REQUIRE(result.has_value());
  REQUIRE(result->scheme == "http");
  REQUIRE(result->host == "example.com");
  REQUIRE(result->port == 80);
  REQUIRE(result->path == "/");
  REQUIRE_FALSE(result->query.has_value());
  REQUIRE_FALSE(result->ipv6Literal);
  REQUIRE_FALSE(result->hasExplicitPort);
  REQUIRE_FALSE(result->hasQuery);
}

TEST_CASE("parseAbsoluteUrl parses basic https URL", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("https://example.com");
  REQUIRE(result.has_value());
  REQUIRE(result->scheme == "https");
  REQUIRE(result->host == "example.com");
  REQUIRE(result->port == 443);
  REQUIRE(result->path == "/");
  REQUIRE_FALSE(result->query.has_value());
  REQUIRE_FALSE(result->ipv6Literal);
  REQUIRE_FALSE(result->hasExplicitPort);
  REQUIRE_FALSE(result->hasQuery);
}

TEST_CASE("parseAbsoluteUrl parses URL with path", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("https://example.com/path/to/resource");
  REQUIRE(result.has_value());
  REQUIRE(result->scheme == "https");
  REQUIRE(result->host == "example.com");
  REQUIRE(result->port == 443);
  REQUIRE(result->path == "/path/to/resource");
  REQUIRE_FALSE(result->query.has_value());
}

TEST_CASE("parseAbsoluteUrl parses URL with explicit port", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("https://example.com:8443/path");
  REQUIRE(result.has_value());
  REQUIRE(result->scheme == "https");
  REQUIRE(result->host == "example.com");
  REQUIRE(result->port == 8443);
  REQUIRE(result->path == "/path");
  REQUIRE(result->hasExplicitPort);
}

TEST_CASE("parseAbsoluteUrl parses URL with query string", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("https://example.com/path?key=value&foo=bar");
  REQUIRE(result.has_value());
  REQUIRE(result->scheme == "https");
  REQUIRE(result->host == "example.com");
  REQUIRE(result->port == 443);
  REQUIRE(result->path == "/path");
  REQUIRE(result->query.has_value());
  REQUIRE(*result->query == "key=value&foo=bar");
  REQUIRE(result->hasQuery);
}

TEST_CASE("parseAbsoluteUrl parses URL with query only (no path)", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("https://example.com?query=value");
  REQUIRE(result.has_value());
  REQUIRE(result->path == "/");
  REQUIRE(result->query.has_value());
  REQUIRE(*result->query == "query=value");
}

TEST_CASE("parseAbsoluteUrl ignores fragment", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("https://example.com/path#fragment");
  REQUIRE(result.has_value());
  REQUIRE(result->path == "/path");
  REQUIRE_FALSE(result->query.has_value());

  const auto result2 = parseAbsoluteUrl("https://example.com/path?query=value#fragment");
  REQUIRE(result2.has_value());
  REQUIRE(result2->path == "/path");
  REQUIRE(result2->query.has_value());
  REQUIRE(*result2->query == "query=value");
}

TEST_CASE("parseAbsoluteUrl lowercases scheme and host", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("HTTPS://EXAMPLE.COM/PATH");
  REQUIRE(result.has_value());
  REQUIRE(result->scheme == "https");
  REQUIRE(result->host == "example.com");
  REQUIRE(result->path == "/PATH");  // Path case is preserved
}

TEST_CASE("parseAbsoluteUrl parses IPv4 address", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("http://192.168.1.1/path");
  REQUIRE(result.has_value());
  REQUIRE(result->scheme == "http");
  REQUIRE(result->host == "192.168.1.1");
  REQUIRE(result->port == 80);
  REQUIRE(result->path == "/path");
  REQUIRE_FALSE(result->ipv6Literal);
}

TEST_CASE("parseAbsoluteUrl parses IPv4 with explicit port", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("http://192.168.1.1:8080/path");
  REQUIRE(result.has_value());
  REQUIRE(result->host == "192.168.1.1");
  REQUIRE(result->port == 8080);
  REQUIRE(result->hasExplicitPort);
}

TEST_CASE("parseAbsoluteUrl parses IPv6 literal", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("http://[::1]/path");
  REQUIRE(result.has_value());
  REQUIRE(result->scheme == "http");
  REQUIRE(result->host == "::1");
  REQUIRE(result->port == 80);
  REQUIRE(result->path == "/path");
  REQUIRE(result->ipv6Literal);
  REQUIRE_FALSE(result->hasExplicitPort);
}

TEST_CASE("parseAbsoluteUrl parses IPv6 literal with port", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("http://[::1]:8080/path");
  REQUIRE(result.has_value());
  REQUIRE(result->host == "::1");
  REQUIRE(result->port == 8080);
  REQUIRE(result->ipv6Literal);
  REQUIRE(result->hasExplicitPort);
}

TEST_CASE("parseAbsoluteUrl parses IPv6 literal with zone ID", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("http://[fe80::1%25eth0]/path");
  REQUIRE(result.has_value());
  REQUIRE(result->host == "fe80::1%25eth0");
  REQUIRE(result->ipv6Literal);
}

TEST_CASE("parseAbsoluteUrl parses various IPv6 formats", "[detail][url]")
{
  // Full IPv6 address
  auto result = parseAbsoluteUrl("http://[2001:db8:85a3::8a2e:370:7334]/path");
  REQUIRE(result.has_value());
  REQUIRE(result->host == "2001:db8:85a3::8a2e:370:7334");
  REQUIRE(result->ipv6Literal);

  // IPv6 with port
  result = parseAbsoluteUrl("https://[2001:db8::1]:9443/api");
  REQUIRE(result.has_value());
  REQUIRE(result->host == "2001:db8::1");
  REQUIRE(result->port == 9443);
  REQUIRE(result->hasExplicitPort);
}

TEST_CASE("parseAbsoluteUrl rejects unclosed IPv6 bracket", "[detail][url]")
{
  REQUIRE_FALSE(parseAbsoluteUrl("http://[::1/path").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("http://[2001:db8::1/path").has_value());
}

TEST_CASE("parseAbsoluteUrl rejects empty IPv6 address", "[detail][url]")
{
  REQUIRE_FALSE(parseAbsoluteUrl("http://[]/path").has_value());
}

TEST_CASE("parseAbsoluteUrl rejects invalid character after IPv6 bracket", "[detail][url]")
{
  REQUIRE_FALSE(parseAbsoluteUrl("http://[::1]extra/path").has_value());
}

TEST_CASE("parseAbsoluteUrl rejects userinfo", "[detail][url]")
{
  // Basic userinfo rejection
  REQUIRE_FALSE(parseAbsoluteUrl("http://user@example.com").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("http://user:pass@example.com").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("https://user@example.com/path").has_value());

  // Userinfo with port
  REQUIRE_FALSE(parseAbsoluteUrl("http://user@example.com:8080").has_value());

  // Userinfo with IPv6
  REQUIRE_FALSE(parseAbsoluteUrl("http://user@[::1]/path").has_value());
}

TEST_CASE("parseAbsoluteUrl rejects whitespace", "[detail][url]")
{
  // Space
  REQUIRE_FALSE(parseAbsoluteUrl("http://example .com").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("http:// example.com").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("http://example.com /path").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("http://example.com/path ").has_value());

  // Tab
  REQUIRE_FALSE(parseAbsoluteUrl("http://example\t.com").has_value());

  // Newline
  REQUIRE_FALSE(parseAbsoluteUrl("http://example\n.com").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("http://example\r\n.com").has_value());

  // Form feed
  REQUIRE_FALSE(parseAbsoluteUrl("http://example\x0c.com").has_value());
}

TEST_CASE("parseAbsoluteUrl rejects control characters", "[detail][url]")
{
  // NUL byte
  REQUIRE_FALSE(parseAbsoluteUrl(std::string("http://example\x00.com", 20)).has_value());

  // DEL character (0x7F)
  REQUIRE_FALSE(parseAbsoluteUrl("http://example\x7f.com").has_value());

  // Various control characters
  for (int c = 0x01; c <= 0x1f; ++c)
  {
    std::string url = "http://example";
    url.push_back(static_cast<char>(c));
    url += ".com";
    REQUIRE_FALSE(parseAbsoluteUrl(url).has_value());
  }
}

TEST_CASE("parseAbsoluteUrl validates scheme characters", "[detail][url]")
{
  // Valid schemes
  REQUIRE(parseAbsoluteUrl("http://example.com").has_value());
  REQUIRE(parseAbsoluteUrl("https://example.com").has_value());
  REQUIRE(parseAbsoluteUrl("ftp://example.com").has_value());
  REQUIRE(parseAbsoluteUrl("custom-scheme://example.com").has_value());
  REQUIRE(parseAbsoluteUrl("custom+scheme://example.com").has_value());
  REQUIRE(parseAbsoluteUrl("custom.scheme://example.com").has_value());

  // Scheme must start with a letter
  REQUIRE_FALSE(parseAbsoluteUrl("1http://example.com").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("-http://example.com").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("+http://example.com").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl(".http://example.com").has_value());

  // Invalid characters in scheme
  REQUIRE_FALSE(parseAbsoluteUrl("http$://example.com").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("http%://example.com").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("http@://example.com").has_value());
}

TEST_CASE("parseAbsoluteUrl validates port numbers", "[detail][url]")
{
  // Valid ports
  REQUIRE(parseAbsoluteUrl("http://example.com:1").has_value());
  REQUIRE(parseAbsoluteUrl("http://example.com:80").has_value());
  REQUIRE(parseAbsoluteUrl("http://example.com:65535").has_value());

  // Port 0 is valid
  REQUIRE(parseAbsoluteUrl("http://example.com:0").has_value());

  // Port too large (overflow)
  REQUIRE_FALSE(parseAbsoluteUrl("http://example.com:65536").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("http://example.com:99999").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("http://example.com:100000").has_value());

  // Invalid port characters
  REQUIRE_FALSE(parseAbsoluteUrl("http://example.com:80a").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("http://example.com:8a0").has_value());
  REQUIRE_FALSE(parseAbsoluteUrl("http://example.com:abc").has_value());

  // Empty port
  REQUIRE_FALSE(parseAbsoluteUrl("http://example.com:").has_value());
}

TEST_CASE("parseAbsoluteUrl handles default ports for known schemes", "[detail][url]")
{
  auto result = parseAbsoluteUrl("http://example.com");
  REQUIRE(result.has_value());
  REQUIRE(result->port == 80);

  result = parseAbsoluteUrl("https://example.com");
  REQUIRE(result.has_value());
  REQUIRE(result->port == 443);

  result = parseAbsoluteUrl("ftp://example.com");
  REQUIRE(result.has_value());
  REQUIRE(result->port == 21);

  result = parseAbsoluteUrl("ftps://example.com");
  REQUIRE(result.has_value());
  REQUIRE(result->port == 990);

  result = parseAbsoluteUrl("ws://example.com");
  REQUIRE(result.has_value());
  REQUIRE(result->port == 80);

  result = parseAbsoluteUrl("wss://example.com");
  REQUIRE(result.has_value());
  REQUIRE(result->port == 443);

  // Unknown scheme has no default port
  result = parseAbsoluteUrl("unknown://example.com");
  REQUIRE(result.has_value());
  REQUIRE(result->port == 0);
}

TEST_CASE("parseAbsoluteUrl handles complex URLs", "[detail][url]")
{
  const auto result = parseAbsoluteUrl("https://user:pass@example.com:8443/path/to/resource?query=value&other=test#fragment");
  // Should reject due to userinfo
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseAbsoluteUrl handles various path formats", "[detail][url]")
{
  auto result = parseAbsoluteUrl("http://example.com/");
  REQUIRE(result.has_value());
  REQUIRE(result->path == "/");

  result = parseAbsoluteUrl("http://example.com/a");
  REQUIRE(result.has_value());
  REQUIRE(result->path == "/a");

  result = parseAbsoluteUrl("http://example.com/a/b/c");
  REQUIRE(result.has_value());
  REQUIRE(result->path == "/a/b/c");

  result = parseAbsoluteUrl("http://example.com/a/b/c/");
  REQUIRE(result.has_value());
  REQUIRE(result->path == "/a/b/c/");
}

TEST_CASE("parseAbsoluteUrl handles edge case hostnames", "[detail][url]")
{
  // Single character host
  auto result = parseAbsoluteUrl("http://x/path");
  REQUIRE(result.has_value());
  REQUIRE(result->host == "x");

  // Very long hostname (valid)
  std::string longHost(253, 'a');
  std::string url = "http://" + longHost + "/path";
  result = parseAbsoluteUrl(url);
  REQUIRE(result.has_value());
  REQUIRE(result->host == longHost);

  // Host with numbers
  result = parseAbsoluteUrl("http://example123.com/path");
  REQUIRE(result.has_value());
  REQUIRE(result->host == "example123.com");

  // Host with hyphens
  result = parseAbsoluteUrl("http://my-example-host.com/path");
  REQUIRE(result.has_value());
  REQUIRE(result->host == "my-example-host.com");
}

TEST_CASE("parseAbsoluteUrl preserves query string edge cases", "[detail][url]")
{
  // Empty query value
  auto result = parseAbsoluteUrl("http://example.com/path?key=");
  REQUIRE(result.has_value());
  REQUIRE(result->query.has_value());
  REQUIRE(*result->query == "key=");

  // Empty query key (edge case)
  result = parseAbsoluteUrl("http://example.com/path?=value");
  REQUIRE(result.has_value());
  REQUIRE(result->query.has_value());
  REQUIRE(*result->query == "=value");

  // Just question mark (empty query)
  result = parseAbsoluteUrl("http://example.com/path?");
  REQUIRE(result.has_value());
  REQUIRE(result->query.has_value());
  REQUIRE(*result->query == "");

  // Query with special characters
  result = parseAbsoluteUrl("http://example.com/path?key=%20value");
  REQUIRE(result.has_value());
  REQUIRE(result->query.has_value());
  REQUIRE(*result->query == "key=%20value");

  // Multiple query parameters
  result = parseAbsoluteUrl("http://example.com/path?a=1&b=2&c=3");
  REQUIRE(result.has_value());
  REQUIRE(result->query.has_value());
  REQUIRE(*result->query == "a=1&b=2&c=3");
}

// NOLINTEND(readability-function-cognitive-complexity)
