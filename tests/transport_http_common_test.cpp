#include <cstdint>
#include <iterator>
#include <string>

#include <boost/beast/http.hpp>  // NOLINT(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <mcp/security/origin_policy.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/version.hpp>

// NOLINTBEGIN(misc-include-cleaner, llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness, bugprone-argument-comment)

namespace
{

namespace beast_http = boost::beast::http;
namespace mcp_http = mcp::transport::http;

class HttpValidationHarness
{
public:
  mcp_http::RequestValidationOptions options;

  auto handle(const beast_http::request<beast_http::string_body> &request) const -> mcp_http::RequestValidationResult
  {
    return mcp_http::validateServerRequest(toHeaders(request), options);
  }

private:
  static auto toHeaders(const beast_http::request<beast_http::string_body> &request) -> mcp_http::HeaderList
  {
    mcp_http::HeaderList headers;
    headers.reserve(static_cast<std::size_t>(std::distance(request.base().begin(), request.base().end())));

    for (const auto &header : request.base())
    {
      headers.push_back(mcp_http::Header {std::string(header.name_string()), std::string(header.value())});
    }

    return headers;
  }
};

auto makeBaseRequest() -> beast_http::request<beast_http::string_body>
{
  beast_http::request<beast_http::string_body> request {beast_http::verb::post, "/mcp", 11};
  request.set(beast_http::field::origin, "http://localhost:6274");
  request.set("MCP-Protocol-Version", std::string(mcp::kLatestProtocolVersion));
  return request;
}

}  // namespace

TEST_CASE("Streamable HTTP rejects invalid or unsupported MCP-Protocol-Version", "[transport][http][common]")
{
  HttpValidationHarness harness;
  harness.options.supportedProtocolVersions = {std::string(mcp::kLatestProtocolVersion)};

  SECTION("Malformed protocol version header returns HTTP 400")
  {
    auto request = makeBaseRequest();
    request.set("MCP-Protocol-Version", "2025/11/25");

    const auto result = harness.handle(request);
    REQUIRE_FALSE(result.accepted);
    REQUIRE(result.statusCode == static_cast<std::uint16_t>(beast_http::status::bad_request));
  }

  SECTION("Unsupported protocol version header returns HTTP 400")
  {
    auto request = makeBaseRequest();
    request.set("MCP-Protocol-Version", "1900-01-01");

    const auto result = harness.handle(request);
    REQUIRE_FALSE(result.accepted);
    REQUIRE(result.statusCode == static_cast<std::uint16_t>(beast_http::status::bad_request));
  }
}

TEST_CASE("Streamable HTTP rejects invalid Origin with HTTP 403", "[transport][http][common]")
{
  HttpValidationHarness harness;

  auto request = makeBaseRequest();
  request.set(beast_http::field::origin, "https://evil.example");

  const auto result = harness.handle(request);
  REQUIRE_FALSE(result.accepted);
  REQUIRE(result.statusCode == static_cast<std::uint16_t>(beast_http::status::forbidden));
}

TEST_CASE("Streamable HTTP enforces required MCP-Session-Id", "[transport][http][common]")
{
  HttpValidationHarness harness;
  harness.options.sessionRequired = true;
  harness.options.requestKind = mcp_http::RequestKind::kOther;

  auto request = makeBaseRequest();

  const auto result = harness.handle(request);
  REQUIRE_FALSE(result.accepted);
  REQUIRE(result.statusCode == static_cast<std::uint16_t>(beast_http::status::bad_request));
}

TEST_CASE("Streamable HTTP returns HTTP 404 for expired or terminated sessions", "[transport][http][common]")
{
  HttpValidationHarness harness;
  harness.options.sessionRequired = true;
  harness.options.requestKind = mcp_http::RequestKind::kOther;

  auto request = makeBaseRequest();
  request.set("MCP-Session-Id", "session-123");

  SECTION("Expired session")
  {
    harness.options.sessionResolver = [](std::string_view) -> mcp_http::SessionResolution
    {
      return {
        mcp_http::SessionLookupState::kExpired,
        std::nullopt,
      };
    };

    const auto result = harness.handle(request);
    REQUIRE_FALSE(result.accepted);
    REQUIRE(result.statusCode == static_cast<std::uint16_t>(beast_http::status::not_found));
  }

  SECTION("Terminated session")
  {
    harness.options.sessionResolver = [](std::string_view) -> mcp_http::SessionResolution
    {
      return {
        mcp_http::SessionLookupState::kTerminated,
        std::nullopt,
      };
    };

    const auto result = harness.handle(request);
    REQUIRE_FALSE(result.accepted);
    REQUIRE(result.statusCode == static_cast<std::uint16_t>(beast_http::status::not_found));
  }
}

TEST_CASE("Protocol fallback defaults to 2025-03-26 when no version can be inferred", "[transport][http][common]")
{
  HttpValidationHarness harness;

  auto request = makeBaseRequest();
  request.erase("MCP-Protocol-Version");

  const auto result = harness.handle(request);
  REQUIRE(result.accepted);
  REQUIRE(result.effectiveProtocolVersion == std::string(mcp::kFallbackProtocolVersion));
}

TEST_CASE("Client session and protocol header state captures and replays MCP headers", "[transport][http][common]")
{
  mcp_http::SessionHeaderState sessionState;
  mcp_http::ProtocolVersionHeaderState protocolState;
  mcp_http::HeaderList headers;

  REQUIRE(sessionState.captureFromInitializeResponse("session-visible-123"));
  REQUIRE(protocolState.setNegotiatedProtocolVersion(std::string(mcp::kLatestProtocolVersion)));

  sessionState.replayToRequestHeaders(headers);
  protocolState.replayToRequestHeaders(headers, false);

  REQUIRE(mcp_http::getHeader(headers, mcp_http::kHeaderMcpSessionId).has_value());
  REQUIRE(mcp_http::getHeader(headers, mcp_http::kHeaderMcpProtocolVersion).has_value());

  mcp_http::HeaderList initializeHeaders;
  protocolState.replayToRequestHeaders(initializeHeaders, true);
  REQUIRE_FALSE(mcp_http::getHeader(initializeHeaders, mcp_http::kHeaderMcpProtocolVersion).has_value());
}

TEST_CASE("Origin policy defaults to localhost-only allowlist", "[transport][http][common]")
{
  mcp::security::OriginPolicy policy;

  REQUIRE(mcp::security::isOriginAllowed("http://localhost:8080", policy));
  REQUIRE(mcp::security::isOriginAllowed("http://127.0.0.1:9000", policy));
  REQUIRE_FALSE(mcp::security::isOriginAllowed("https://example.com", policy));
}

// NOLINTEND(misc-include-cleaner, llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness, bugprone-argument-comment)
