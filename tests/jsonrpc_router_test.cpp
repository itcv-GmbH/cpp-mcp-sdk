#include <cstdint>
#include <future>
#include <string>
#include <utility>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <mcp/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/jsonrpc/router.hpp>

namespace
{

constexpr std::int64_t kDuplicateRequestId = 7;
constexpr std::int64_t kSharedRequestIdAcrossSenders = 11;

}  // namespace

namespace test_detail
{

auto hasIdValue(const mcp::jsonrpc::RequestId &id, std::int64_t expectedValue) -> bool
{
  if (!std::holds_alternative<std::int64_t>(id))
  {
    return false;
  }

  return std::get<std::int64_t>(id) == expectedValue;
}

auto makeReadyResponseFuture(mcp::jsonrpc::Response response) -> std::future<mcp::jsonrpc::Response>
{
  std::promise<mcp::jsonrpc::Response> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

}  // namespace test_detail

TEST_CASE("Router enforces unique request ids per sender", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  router.registerRequestHandler("ping",
                                [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                {
                                  mcp::jsonrpc::SuccessResponse success;
                                  success.id = request.id;
                                  success.result = mcp::jsonrpc::JsonValue::object();
                                  return test_detail::makeReadyResponseFuture(mcp::jsonrpc::Response {success});
                                });

  mcp::jsonrpc::Request request;
  request.id = kDuplicateRequestId;
  request.method = "ping";

  mcp::jsonrpc::RequestContext sender;
  sender.sessionId = "session-a";

  const mcp::jsonrpc::Response firstResponse = router.dispatchRequest(sender, request).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(firstResponse));

  const mcp::jsonrpc::Response duplicateResponse = router.dispatchRequest(sender, request).get();
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(duplicateResponse));

  const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(duplicateResponse);
  REQUIRE(error.id.has_value());
  if (error.id.has_value())
  {
    REQUIRE(test_detail::hasIdValue(*error.id, kDuplicateRequestId));
  }
  REQUIRE(error.error.code == static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kInvalidRequest));
}

TEST_CASE("Router allows same request id from different senders", "[jsonrpc][router]")
{
  mcp::jsonrpc::Router router;
  router.registerRequestHandler("ping",
                                [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                {
                                  mcp::jsonrpc::SuccessResponse success;
                                  success.id = request.id;
                                  success.result = mcp::jsonrpc::JsonValue::object();
                                  return test_detail::makeReadyResponseFuture(mcp::jsonrpc::Response {success});
                                });

  mcp::jsonrpc::Request request;
  request.id = kSharedRequestIdAcrossSenders;
  request.method = "ping";

  mcp::jsonrpc::RequestContext senderA;
  senderA.sessionId = "session-a";

  mcp::jsonrpc::RequestContext senderB;
  senderB.sessionId = "session-b";

  const mcp::jsonrpc::Response responseA = router.dispatchRequest(senderA, request).get();
  const mcp::jsonrpc::Response responseB = router.dispatchRequest(senderB, request).get();

  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(responseA));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(responseB));
}
