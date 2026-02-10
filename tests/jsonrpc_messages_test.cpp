#include <algorithm>
#include <cstdint>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <mcp/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>

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
