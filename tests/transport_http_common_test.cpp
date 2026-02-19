#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include <boost/beast/http.hpp>  // NOLINT(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/messages.hpp>
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

auto makeJsonRpcRequestBody(std::int64_t id, std::string method) -> std::string
{
  mcp::jsonrpc::Request request;
  request.id = id;
  request.method = std::move(method);
  return mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {request});
}

auto makeServerPostRequest(std::string body) -> mcp_http::ServerRequest
{
  mcp_http::ServerRequest request;
  request.method = mcp_http::ServerRequestMethod::kPost;
  request.path = "/mcp";
  request.body = std::move(body);
  mcp_http::setHeader(request.headers, mcp_http::kHeaderContentType, "application/json");
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

TEST_CASE("setHeader and getHeader are case-insensitive and overwrite existing values", "[transport][http][common]")
{
  mcp_http::HeaderList headers;

  SECTION("Case-insensitive header name matching")
  {
    mcp_http::setHeader(headers, "Content-Type", "application/json");

    // All case variations should find the same header
    REQUIRE(mcp_http::getHeader(headers, "Content-Type").value() == "application/json");
    REQUIRE(mcp_http::getHeader(headers, "content-type").value() == "application/json");
    REQUIRE(mcp_http::getHeader(headers, "CONTENT-TYPE").value() == "application/json");
    REQUIRE(mcp_http::getHeader(headers, "CoNtEnT-TyPe").value() == "application/json");
  }

  SECTION("Overwriting existing headers preserves case-insensitivity")
  {
    mcp_http::setHeader(headers, "X-Custom-Header", "original-value");
    REQUIRE(mcp_http::getHeader(headers, "x-custom-header").value() == "original-value");

    // Overwrite with different case
    mcp_http::setHeader(headers, "X-CUSTOM-HEADER", "updated-value");

    // Should only have one header (overwrite, not append)
    REQUIRE(headers.size() == 1);
    REQUIRE(mcp_http::getHeader(headers, "x-custom-header").value() == "updated-value");
    REQUIRE(mcp_http::getHeader(headers, "X-Custom-Header").value() == "updated-value");
  }

  SECTION("MCP specific headers with various cases")
  {
    mcp_http::setHeader(headers, "mcp-session-id", "session-abc");
    REQUIRE(mcp_http::getHeader(headers, "MCP-Session-Id").value() == "session-abc");
    REQUIRE(mcp_http::getHeader(headers, "mcp-session-id").value() == "session-abc");
    REQUIRE(mcp_http::getHeader(headers, "Mcp-Session-Id").value() == "session-abc");

    mcp_http::setHeader(headers, "MCP-PROTOCOL-VERSION", "2025-11-25");
    REQUIRE(mcp_http::getHeader(headers, "mcp-protocol-version").value() == "2025-11-25");
    REQUIRE(mcp_http::getHeader(headers, "Mcp-Protocol-Version").value() == "2025-11-25");
  }

  SECTION("getHeader returns nullopt for non-existent headers")
  {
    REQUIRE_FALSE(mcp_http::getHeader(headers, "Non-Existent-Header").has_value());
    REQUIRE_FALSE(mcp_http::getHeader(headers, "X-Unknown").has_value());
  }
}

TEST_CASE("SessionHeaderState::captureFromInitializeResponse trims ASCII whitespace and validates session IDs", "[transport][http][common]")
{
  mcp_http::SessionHeaderState state;

  SECTION("Valid visible-ASCII session ID is captured")
  {
    REQUIRE(state.captureFromInitializeResponse("valid-session-123"));
    REQUIRE(state.sessionId().has_value());
    REQUIRE(state.sessionId().value() == "valid-session-123");
    REQUIRE(state.replayOnSubsequentRequests());
  }

  SECTION("Leading ASCII whitespace is trimmed")
  {
    REQUIRE(state.captureFromInitializeResponse("   session-with-leading-space"));
    REQUIRE(state.sessionId().has_value());
    REQUIRE(state.sessionId().value() == "session-with-leading-space");
  }

  SECTION("Trailing ASCII whitespace is trimmed")
  {
    REQUIRE(state.captureFromInitializeResponse("session-with-trailing-space   "));
    REQUIRE(state.sessionId().has_value());
    REQUIRE(state.sessionId().value() == "session-with-trailing-space");
  }

  SECTION("Both leading and trailing ASCII whitespace is trimmed")
  {
    REQUIRE(state.captureFromInitializeResponse("  \t\nsession-with-both-spaces\r\n  "));
    REQUIRE(state.sessionId().has_value());
    REQUIRE(state.sessionId().value() == "session-with-both-spaces");
  }

  SECTION("Internal whitespace is rejected (space is not visible ASCII)")
  {
    // Space (0x20) is below visible ASCII range (0x21-0x7E), so session IDs with spaces are invalid
    REQUIRE_FALSE(state.captureFromInitializeResponse("session with internal spaces"));
    REQUIRE_FALSE(state.sessionId().has_value());
  }

  SECTION("Internal tab characters are rejected (tab is not visible ASCII)")
  {
    // Tab (0x09) is below visible ASCII range, so session IDs with tabs are invalid
    REQUIRE_FALSE(state.captureFromInitializeResponse("session\twith\ttabs"));
    REQUIRE_FALSE(state.sessionId().has_value());
  }

  SECTION("Only visible ASCII characters are valid in session IDs")
  {
    // Visible ASCII is 0x21-0x7E ('!' to '~')
    REQUIRE(state.captureFromInitializeResponse("!visible~ASCII~only!"));
    REQUIRE(state.sessionId().value() == "!visible~ASCII~only!");

    // Space character (0x20) just below visible range is rejected
    REQUIRE_FALSE(state.captureFromInitializeResponse("contains space"));

    // DEL character (0x7F) just above visible range is rejected
    REQUIRE_FALSE(state.captureFromInitializeResponse("session\x7Fid"));
  }

  SECTION("Empty string after trimming is rejected")
  {
    REQUIRE_FALSE(state.captureFromInitializeResponse("     "));
    REQUIRE_FALSE(state.sessionId().has_value());
    REQUIRE_FALSE(state.replayOnSubsequentRequests());
  }

  SECTION("Nullopt clears state and returns true")
  {
    // First set a valid session
    REQUIRE(state.captureFromInitializeResponse("valid-session"));
    REQUIRE(state.sessionId().has_value());

    // Then clear with nullopt
    REQUIRE(state.captureFromInitializeResponse(std::nullopt));
    REQUIRE_FALSE(state.sessionId().has_value());
    REQUIRE_FALSE(state.replayOnSubsequentRequests());
  }

  SECTION("Space character (0x20) is trimmed and rejected if only content")
  {
    REQUIRE_FALSE(state.captureFromInitializeResponse(" "));
    REQUIRE_FALSE(state.sessionId().has_value());
  }

  SECTION("Tab character (0x09) is trimmed and rejected if only content")
  {
    REQUIRE_FALSE(state.captureFromInitializeResponse("\t"));
    REQUIRE_FALSE(state.sessionId().has_value());
  }

  SECTION("Newline characters are trimmed")
  {
    REQUIRE(state.captureFromInitializeResponse("\n\rsession-id\r\n"));
    REQUIRE(state.sessionId().value() == "session-id");
  }
}

TEST_CASE("isValidProtocolVersion rejects malformed dates and non-digit characters", "[transport][http][common]")
{
  SECTION("Valid protocol versions are accepted")
  {
    REQUIRE(mcp_http::isValidProtocolVersion("2025-11-25"));
    REQUIRE(mcp_http::isValidProtocolVersion("2024-11-05"));
    REQUIRE(mcp_http::isValidProtocolVersion("2025-03-26"));
    REQUIRE(mcp_http::isValidProtocolVersion("2020-01-01"));
    REQUIRE(mcp_http::isValidProtocolVersion("9999-99-99"));
  }

  SECTION("Wrong length is rejected")
  {
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-11-2"));  // Too short
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-11-255"));  // Too long
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-1-25"));  // Month single digit
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("202-11-25"));  // Year short
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion(""));  // Empty
  }

  SECTION("Wrong dash positions are rejected")
  {
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("202511-25"));  // Missing first dash
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-1125"));  // Missing second dash
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025--1125"));  // Double dash
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("20251125"));  // No dashes
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2-025-1125"));  // Wrong dash position 1
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("20251-1-255"));  // Wrong dash position 2
  }

  SECTION("Slash instead of dash is rejected")
  {
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025/11/25"));
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-11/25"));
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025/11-25"));
  }

  SECTION("Non-digit characters are rejected")
  {
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("AAAA-AA-AA"));  // All letters
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-AA-25"));  // Letters in month
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-11-AA"));  // Letters in day
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("AAAA-11-25"));  // Letters in year
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-0X-25"));  // Hex-like
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-11-2X"));  // Mixed alphanumeric
  }

  SECTION("Special characters are rejected")
  {
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-11-2!"));  // Exclamation
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-11-@5"));  // At symbol
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-#1-25"));  // Hash
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("$025-11-25"));  // Dollar sign
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-11-%5"));  // Percent
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-^1-25"));  // Caret
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("&025-11-25"));  // Ampersand
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-*1-25"));  // Asterisk
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-11-(5"));  // Parenthesis
  }

  SECTION("Whitespace is rejected")
  {
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-11-25 "));  // Trailing space
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion(" 2025-11-25"));  // Leading space
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025 -11-25"));  // Space in year
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-11 -25"));  // Space in month
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-11-2 5"));  // Space in day
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025\t11-25"));  // Tab
  }

  SECTION("Unicode characters are rejected")
  {
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025-11-2日"));  // CJK character
    REQUIRE_FALSE(mcp_http::isValidProtocolVersion("2025年11-25"));  // CJK character
  }
}

TEST_CASE("Protocol fallback behavior only applies when version cannot be inferred", "[transport][http][common]")
{
  HttpValidationHarness harness;

  SECTION("Fallback used when no version header and no session resolver")
  {
    auto request = makeBaseRequest();
    request.erase("MCP-Protocol-Version");

    const auto result = harness.handle(request);
    REQUIRE(result.accepted);
    REQUIRE(result.effectiveProtocolVersion == std::string(mcp::kFallbackProtocolVersion));
  }

  SECTION("Fallback used when no version header and session resolver returns no negotiated version")
  {
    harness.options.sessionResolver = [](std::string_view) -> mcp_http::SessionResolution { return {mcp_http::SessionLookupState::kActive, std::nullopt}; };

    auto request = makeBaseRequest();
    request.set("MCP-Session-Id", "session-123");
    request.erase("MCP-Protocol-Version");

    const auto result = harness.handle(request);
    REQUIRE(result.accepted);
    REQUIRE(result.effectiveProtocolVersion == std::string(mcp::kFallbackProtocolVersion));
  }

  SECTION("Version from session resolver used when no header present")
  {
    harness.options.sessionResolver = [](std::string_view) -> mcp_http::SessionResolution { return {mcp_http::SessionLookupState::kActive, std::string("2024-11-05")}; };

    auto request = makeBaseRequest();
    request.set("MCP-Session-Id", "session-123");
    request.erase("MCP-Protocol-Version");

    const auto result = harness.handle(request);
    REQUIRE(result.accepted);
    REQUIRE(result.effectiveProtocolVersion == "2024-11-05");
  }

  SECTION("Header version takes precedence over session resolver version")
  {
    harness.options.sessionResolver = [](std::string_view) -> mcp_http::SessionResolution { return {mcp_http::SessionLookupState::kActive, std::string("2024-11-05")}; };

    auto request = makeBaseRequest();
    request.set("MCP-Session-Id", "session-123");
    request.set("MCP-Protocol-Version", "2025-11-25");

    const auto result = harness.handle(request);
    REQUIRE(result.accepted);
    REQUIRE(result.effectiveProtocolVersion == "2025-11-25");
  }

  SECTION("Inferred protocol version from options used when no header")
  {
    harness.options.inferredProtocolVersion = std::string("2025-11-25");

    auto request = makeBaseRequest();
    request.erase("MCP-Protocol-Version");

    const auto result = harness.handle(request);
    REQUIRE(result.accepted);
    REQUIRE(result.effectiveProtocolVersion == "2025-11-25");
  }

  SECTION("Inferred protocol version takes precedence over fallback")
  {
    harness.options.inferredProtocolVersion = std::string("2024-11-05");

    auto request = makeBaseRequest();
    request.erase("MCP-Protocol-Version");

    const auto result = harness.handle(request);
    REQUIRE(result.accepted);
    REQUIRE(result.effectiveProtocolVersion == "2024-11-05");
    REQUIRE(result.effectiveProtocolVersion != std::string(mcp::kFallbackProtocolVersion));
  }

  SECTION("Invalid inferred protocol version is rejected")
  {
    harness.options.inferredProtocolVersion = std::string("invalid-version");

    auto request = makeBaseRequest();
    request.erase("MCP-Protocol-Version");

    const auto result = harness.handle(request);
    REQUIRE_FALSE(result.accepted);
    REQUIRE(result.statusCode == static_cast<std::uint16_t>(beast_http::status::bad_request));
  }

  SECTION("Unsupported inferred protocol version is rejected")
  {
    harness.options.inferredProtocolVersion = std::string("1999-01-01");
    harness.options.supportedProtocolVersions = {std::string(mcp::kLatestProtocolVersion)};

    auto request = makeBaseRequest();
    request.erase("MCP-Protocol-Version");

    const auto result = harness.handle(request);
    REQUIRE_FALSE(result.accepted);
    REQUIRE(result.statusCode == static_cast<std::uint16_t>(beast_http::status::bad_request));
  }

  SECTION("Version header with whitespace is trimmed and accepted")
  {
    auto request = makeBaseRequest();
    request.set("MCP-Protocol-Version", "  2025-11-25  ");

    const auto result = harness.handle(request);
    REQUIRE(result.accepted);
    REQUIRE(result.effectiveProtocolVersion == "2025-11-25");
  }

  SECTION("Initialize request does not require session even when sessionRequired is true")
  {
    harness.options.sessionRequired = true;
    harness.options.requestKind = mcp_http::RequestKind::kInitialize;

    auto request = makeBaseRequest();
    request.erase("MCP-Session-Id");

    const auto result = harness.handle(request);
    REQUIRE(result.accepted);
  }
}

TEST_CASE("Streamable HTTP avoids deadlock when handler enqueues server message", "[transport][http][common]")
{
  using namespace std::chrono_literals;

  mcp_http::StreamableHttpServer server;
  std::atomic<int> enqueueCalls = 0;

  server.setRequestHandler(
    [&server, &enqueueCalls](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> mcp_http::StreamableRequestResult
    {
      mcp::jsonrpc::Notification queuedFromHandler;
      queuedFromHandler.method = "notifications/queued-from-handler";
      enqueueCalls.fetch_add(1);
      static_cast<void>(server.enqueueServerMessage(mcp::jsonrpc::Message {queuedFromHandler}));

      mcp_http::StreamableRequestResult result;
      result.useSse = true;

      mcp::jsonrpc::Notification preResponse;
      preResponse.method = "notifications/pre-response";
      result.preResponseMessages.push_back(mcp::jsonrpc::Message {preResponse});

      mcp::jsonrpc::SuccessResponse response;
      response.id = request.id;
      response.result = mcp::jsonrpc::JsonValue::object();
      response.result["ok"] = true;
      result.response = response;
      return result;
    });

  std::future<mcp_http::ServerResponse> responseFuture =
    std::async(std::launch::async, [&server]() -> mcp_http::ServerResponse { return server.handleRequest(makeServerPostRequest(makeJsonRpcRequestBody(17, "ping"))); });

  REQUIRE(responseFuture.wait_for(500ms) == std::future_status::ready);

  const mcp_http::ServerResponse postResponse = responseFuture.get();
  REQUIRE(postResponse.statusCode == 200);
  REQUIRE(postResponse.sse.has_value());
  REQUIRE(postResponse.sse->events.size() == 3);
  REQUIRE(enqueueCalls.load() == 1);

  const mcp::jsonrpc::Message preResponseMessage = mcp::jsonrpc::parseMessage(postResponse.sse->events[1].data);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Notification>(preResponseMessage));
  REQUIRE(std::get<mcp::jsonrpc::Notification>(preResponseMessage).method == "notifications/pre-response");

  const mcp::jsonrpc::Message responseMessage = mcp::jsonrpc::parseMessage(postResponse.sse->events[2].data);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(responseMessage));
  REQUIRE(std::get<mcp::jsonrpc::SuccessResponse>(responseMessage).id == mcp::jsonrpc::RequestId {std::int64_t {17}});
}

// NOLINTEND(misc-include-cleaner, llvm-prefer-static-over-anonymous-namespace, readability-function-cognitive-complexity, cppcoreguidelines-avoid-magic-numbers,
// readability-magic-numbers, misc-const-correctness, bugprone-argument-comment)
