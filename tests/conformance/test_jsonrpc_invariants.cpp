#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/all.hpp>

namespace
{

auto hasIntId(const mcp::jsonrpc::RequestId &id, std::int64_t expected) -> bool
{
  return std::holds_alternative<std::int64_t>(id) && std::get<std::int64_t>(id) == expected;
}

}  // namespace

TEST_CASE("JSON-RPC request and notification invariants", "[conformance][jsonrpc]")
{
  SECTION("request requires non-null id")
  {
    REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":null,"method":"ping"})"), mcp::jsonrpc::MessageValidationError);
  }

  SECTION("notification must not include id")
  {
    const mcp::jsonrpc::Message notificationMessage = mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","method":"notifications/ping"})");
    REQUIRE(std::holds_alternative<mcp::jsonrpc::Notification>(notificationMessage));

    REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","method":"notifications/ping","id":null})"), mcp::jsonrpc::MessageValidationError);
  }

  SECTION("request must not include result or error")
  {
    REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":1,"method":"ping","result":{}})"), mcp::jsonrpc::MessageValidationError);
    REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":1,"method":"ping","error":{"code":-32603,"message":"Internal error"}})"),
                      mcp::jsonrpc::MessageValidationError);
  }
}

TEST_CASE("JSON-RPC response invariants", "[conformance][jsonrpc]")
{
  SECTION("success response includes id and result")
  {
    const mcp::jsonrpc::Message successMessage = mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":7,"result":{}})");
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(successMessage));

    const auto &success = std::get<mcp::jsonrpc::SuccessResponse>(successMessage);
    REQUIRE(hasIntId(success.id, 7));
  }

  SECTION("response must include exactly one of result or error")
  {
    REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":1,"result":{},"error":{"code":-32603,"message":"Internal error"}})"),
                      mcp::jsonrpc::MessageValidationError);
    REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":1})"), mcp::jsonrpc::MessageValidationError);
  }

  SECTION("response must not include params")
  {
    REQUIRE_THROWS_AS(mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":1,"result":{},"params":{}})"), mcp::jsonrpc::MessageValidationError);
  }

  SECTION("error response allows unknown id using null")
  {
    const mcp::jsonrpc::Message errorMessage = mcp::jsonrpc::parseMessage(R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Invalid Request"}})");
    REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(errorMessage));

    const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(errorMessage);
    REQUIRE(error.hasUnknownId);
    REQUIRE_FALSE(error.id.has_value());
  }
}
