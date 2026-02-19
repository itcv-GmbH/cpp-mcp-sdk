#include <algorithm>
#include <cstdint>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <mcp/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>

// NOLINTBEGIN(readability-function-cognitive-complexity)

constexpr std::int64_t kTestRequestId = 42;

namespace test_detail
{

auto hasIdValue(const mcp::jsonrpc::RequestId &id, const std::string &expectedValue) -> bool
{
  if (!std::holds_alternative<std::string>(id))
  {
    return false;
  }

  return std::get<std::string>(id) == expectedValue;
}

auto hasIdValue(const mcp::jsonrpc::RequestId &id, std::int64_t expectedValue) -> bool
{
  if (!std::holds_alternative<std::int64_t>(id))
  {
    return false;
  }

  return std::get<std::int64_t>(id) == expectedValue;
}

auto containsText(const std::string &text, const std::string &needle) -> bool
{
  return std::search(text.begin(), text.end(), needle.begin(), needle.end()) != text.end();
}

auto containsNoChar(const std::string &text, char value) -> bool
{
  return std::find(text.begin(), text.end(), value) == text.end();
}

}  // namespace test_detail

TEST_CASE("Parses request and preserves unknown top-level fields", "[jsonrpc][messages]")  // NOLINT(readability-function-cognitive-complexity)
{
  const std::string input = R"({"jsonrpc":"2.0","id":"req-1","method":"ping","params":{"value":1},"trace":"abc"})";

  const mcp::jsonrpc::Message parsed = mcp::jsonrpc::parseMessage(input);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(parsed));

  const auto &request = std::get<mcp::jsonrpc::Request>(parsed);
  REQUIRE(request.method == "ping");
  REQUIRE(test_detail::hasIdValue(request.id, "req-1"));
  REQUIRE(request.params.has_value());
  if (request.params.has_value())
  {
    const mcp::jsonrpc::JsonValue &params = *request.params;
    REQUIRE(params.at("value").as<int>() == 1);
  }
  REQUIRE(request.additionalProperties.contains("trace"));
  REQUIRE(request.additionalProperties.at("trace").as<std::string>() == "abc");

  const std::string encoded = mcp::jsonrpc::serializeMessage(parsed);
  REQUIRE(test_detail::containsText(encoded, "\"trace\":\"abc\""));
}

TEST_CASE("Parses notification without id", "[jsonrpc][messages]")
{
  const std::string input = R"({"jsonrpc":"2.0","method":"notifications/ping","params":{}})";

  const mcp::jsonrpc::Message parsed = mcp::jsonrpc::parseMessage(input);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Notification>(parsed));

  const auto &notification = std::get<mcp::jsonrpc::Notification>(parsed);
  REQUIRE(notification.method == "notifications/ping");
  REQUIRE(notification.params.has_value());
}

TEST_CASE("Message parser enforces non-null ids but not cross-message uniqueness", "[jsonrpc][messages]")
{
  const mcp::jsonrpc::Message first = mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":1,"method":"ping"})");
  const mcp::jsonrpc::Message second = mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":1,"method":"ping"})");

  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(first));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(second));
  REQUIRE(test_detail::hasIdValue(std::get<mcp::jsonrpc::Request>(first).id, std::int64_t {1}));
  REQUIRE(test_detail::hasIdValue(std::get<mcp::jsonrpc::Request>(second).id, std::int64_t {1}));
}

TEST_CASE("Rejects invalid request and response shapes", "[jsonrpc][messages]")
{
  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"1.0","method":"ping"})"), mcp::jsonrpc::MessageValidationError);
  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","method":"ping","id":null})"), mcp::jsonrpc::MessageValidationError);
  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":1,"result":{},"error":{"code":-32603,"message":"Internal error"}})"), mcp::jsonrpc::MessageValidationError);
  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":1})"), mcp::jsonrpc::MessageValidationError);
}

TEST_CASE("Error response supports unknown id cases", "[jsonrpc][messages]")
{
  const mcp::jsonrpc::Message parsed = mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Invalid Request"}})");

  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(parsed));
  const auto &response = std::get<mcp::jsonrpc::ErrorResponse>(parsed);
  REQUIRE(response.hasUnknownId);
  REQUIRE_FALSE(response.id.has_value());
  REQUIRE(response.error.code == -32600);

  const std::string encoded = mcp::jsonrpc::serializeMessage(parsed);
  REQUIRE(test_detail::containsText(encoded, "\"id\":null"));
}

TEST_CASE("Error helpers produce standard JSON-RPC codes", "[jsonrpc][messages]")
{
  const mcp::JsonRpcError parseError = mcp::jsonrpc::makeParseError();
  REQUIRE(parseError.code == -32700);

  const mcp::JsonRpcError invalidRequestError = mcp::jsonrpc::makeInvalidRequestError();
  REQUIRE(invalidRequestError.code == -32600);

  const mcp::JsonRpcError methodNotFoundError = mcp::jsonrpc::makeMethodNotFoundError();
  REQUIRE(methodNotFoundError.code == -32601);

  const mcp::JsonRpcError invalidParamsError = mcp::jsonrpc::makeInvalidParamsError();
  REQUIRE(invalidParamsError.code == -32602);

  const mcp::JsonRpcError internalError = mcp::jsonrpc::makeInternalError();
  REQUIRE(internalError.code == -32603);
}

TEST_CASE("Single-line encoding mode rejects embedded newlines", "[jsonrpc][messages]")
{
  mcp::jsonrpc::Request request;
  request.id = kTestRequestId;
  request.method = "tools/call";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["text"] = "line1\nline2";

  mcp::jsonrpc::EncodeOptions encodeOptions;
  encodeOptions.disallowEmbeddedNewlines = true;

  const std::string encoded = mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message(request), encodeOptions);

  REQUIRE(test_detail::containsNoChar(encoded, '\n'));
  REQUIRE(test_detail::containsNoChar(encoded, '\r'));
}

TEST_CASE("Response invariants: exactly one of result or error", "[jsonrpc][messages]")
{
  // Response with both result and error should be rejected
  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":1,"result":{},"error":{"code":-32603,"message":"Internal error"}})"), mcp::jsonrpc::MessageValidationError);

  // Response with neither result nor error should be rejected
  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":1})"), mcp::jsonrpc::MessageValidationError);
}

TEST_CASE("Notification invariants: must not accept an id", "[jsonrpc][messages]")
{
  // A message with both method and id is a Request, not a Notification
  const std::string requestWithId = R"({"jsonrpc":"2.0","method":"notifications/ping","params":{},"id":123})";
  const mcp::jsonrpc::Message parsed = mcp::jsonrpc::parseMessage(requestWithId);

  // Should be parsed as a Request, not a Notification
  REQUIRE(std::holds_alternative<mcp::jsonrpc::Request>(parsed));
  const auto &request = std::get<mcp::jsonrpc::Request>(parsed);
  REQUIRE(request.method == "notifications/ping");
  REQUIRE(test_detail::hasIdValue(request.id, std::int64_t {123}));
}

TEST_CASE("Strict UTF-8 enforcement: rejects invalid byte sequences", "[jsonrpc][messages]")
{
  // Invalid UTF-8: continuation byte without leading byte (0x80)
  std::string invalidUtf8;
  invalidUtf8 += '{';  // Valid ASCII
  invalidUtf8 += '"';  // Valid ASCII
  invalidUtf8 += static_cast<char>(0x80);  // Invalid UTF-8 continuation byte
  invalidUtf8 += '"';  // Valid ASCII
  invalidUtf8 += '}';  // Valid ASCII

  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(invalidUtf8), mcp::jsonrpc::MessageValidationError);

  // Invalid UTF-8: truncated multi-byte sequence (0xC0 starts a 2-byte sequence but no continuation)
  std::string truncatedUtf8;
  truncatedUtf8 += '{';  // Valid ASCII
  truncatedUtf8 += '"';  // Valid ASCII
  truncatedUtf8 += static_cast<char>(0xC0);  // Starts 2-byte sequence but no continuation
  truncatedUtf8 += '"';  // Valid ASCII
  truncatedUtf8 += '}';  // Valid ASCII

  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(truncatedUtf8), mcp::jsonrpc::MessageValidationError);

  // Invalid UTF-8: overlong encoding (0xC0 0x80 attempts to encode NULL)
  std::string overlongUtf8;
  overlongUtf8 += '{';  // Valid ASCII
  overlongUtf8 += '"';  // Valid ASCII
  overlongUtf8 += static_cast<char>(0xC0);  // Invalid overlong encoding
  overlongUtf8 += static_cast<char>(0x80);  // Continuation byte
  overlongUtf8 += '"';  // Valid ASCII
  overlongUtf8 += '}';  // Valid ASCII

  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(overlongUtf8), mcp::jsonrpc::MessageValidationError);
}

TEST_CASE("serializeMessage produces single-line payload for stdio framing", "[jsonrpc][messages]")
{
  mcp::jsonrpc::Request request;
  request.id = kTestRequestId;
  request.method = "test/method";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["key"] = "value";

  // Default encoding should produce single-line output (no newlines)
  const std::string defaultEncoded = mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message(request));
  REQUIRE(test_detail::containsNoChar(defaultEncoded, '\n'));
  REQUIRE(test_detail::containsNoChar(defaultEncoded, '\r'));

  // Encoding with explicit options should also produce single-line
  mcp::jsonrpc::EncodeOptions options;
  options.disallowEmbeddedNewlines = true;
  const std::string strictEncoded = mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message(request), options);
  REQUIRE(test_detail::containsNoChar(strictEncoded, '\n'));
  REQUIRE(test_detail::containsNoChar(strictEncoded, '\r'));

  // Verify the content is valid JSON without embedded newlines
  REQUIRE(test_detail::containsText(strictEncoded, "\"jsonrpc\":\"2.0\""));
  REQUIRE(test_detail::containsText(strictEncoded, "\"method\":\"test/method\""));
}

TEST_CASE("Malformed JSON produces ParseError mapping consistently", "[jsonrpc][messages]")
{
  // Missing closing brace
  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":1,"method":"ping")"), mcp::jsonrpc::MessageValidationError);

  // Trailing comma
  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":1,"method":"ping",})"), mcp::jsonrpc::MessageValidationError);

  // Invalid escape sequence
  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":1,"method":"ping\q"})"), mcp::jsonrpc::MessageValidationError);

  // Control character in string (unescaped)
  std::string controlCharJson;
  controlCharJson += '{';  // Valid ASCII
  controlCharJson += '"';  // Valid ASCII
  controlCharJson += 'j';  // Valid ASCII
  controlCharJson += 's';  // Valid ASCII
  controlCharJson += '"';  // Valid ASCII
  controlCharJson += ':';  // Valid ASCII
  controlCharJson += '"';  // Valid ASCII
  controlCharJson += '2';  // Valid ASCII
  controlCharJson += '.';  // Valid ASCII
  controlCharJson += '0';  // Valid ASCII
  controlCharJson += static_cast<char>(0x01);  // Invalid control character
  controlCharJson += '"';  // Valid ASCII
  controlCharJson += '}';  // Valid ASCII

  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(controlCharJson), mcp::jsonrpc::MessageValidationError);

  // Empty input
  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(""), mcp::jsonrpc::MessageValidationError);

  // Whitespace only
  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage("   \n\t  "), mcp::jsonrpc::MessageValidationError);

  // Invalid JSON type (array instead of object)
  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"(["jsonrpc","2.0"])"), mcp::jsonrpc::MessageValidationError);

  // Unquoted key
  REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({jsonrpc:"2.0",id:1,method:"ping"})"), mcp::jsonrpc::MessageValidationError);
}

// NOLINTEND(readability-function-cognitive-complexity)
