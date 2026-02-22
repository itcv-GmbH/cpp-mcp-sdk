#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <mcp/sdk/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/server.hpp>
#include <mcp/sdk/version.hpp>

namespace
{

auto makeInitializeRequest(std::int64_t requestId = 1) -> mcp::jsonrpc::Request
{
  mcp::jsonrpc::Request request;
  request.id = requestId;
  request.method = "initialize";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["protocolVersion"] = std::string(mcp::kLatestProtocolVersion);
  (*request.params)["capabilities"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["clientInfo"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["clientInfo"]["name"] = "conformance-client";
  (*request.params)["clientInfo"]["version"] = "1.0.0";
  return request;
}

auto makeRequest(std::int64_t requestId, std::string method) -> mcp::jsonrpc::Request
{
  mcp::jsonrpc::Request request;
  request.id = requestId;
  request.method = std::move(method);
  request.params = mcp::jsonrpc::JsonValue::object();
  return request;
}

auto dispatchRequest(mcp::Server &server, const mcp::jsonrpc::Request &request) -> mcp::jsonrpc::Response
{
  return server.handleRequest(mcp::jsonrpc::RequestContext {}, request).get();
}

auto assertErrorCode(const mcp::jsonrpc::Response &response, mcp::JsonRpcErrorCode expectedCode) -> void
{
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
  const auto &errorResponse = std::get<mcp::jsonrpc::ErrorResponse>(response);
  REQUIRE(errorResponse.error.code == static_cast<std::int32_t>(expectedCode));
}

}  // namespace

TEST_CASE("Feature methods are gated by negotiated capabilities", "[conformance][capabilities]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::ServerCapabilities(mcp::LoggingCapability {}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt);

  const std::shared_ptr<mcp::Server> server = mcp::Server::create(std::move(configuration));

  const mcp::jsonrpc::Response initializeResponse = dispatchRequest(*server, makeInitializeRequest(1));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initializeResponse));

  mcp::jsonrpc::Notification initialized;
  initialized.method = "notifications/initialized";
  server->handleNotification(mcp::jsonrpc::RequestContext {}, initialized);

  const mcp::jsonrpc::Response toolsResponse = dispatchRequest(*server, makeRequest(2, "tools/list"));
  assertErrorCode(toolsResponse, mcp::JsonRpcErrorCode::kMethodNotFound);

  const mcp::jsonrpc::Response loggingResponse = dispatchRequest(*server, makeRequest(3, "logging/setLevel"));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(loggingResponse));
  const auto &loggingError = std::get<mcp::jsonrpc::ErrorResponse>(loggingResponse);
  REQUIRE(loggingError.error.code != static_cast<std::int32_t>(mcp::JsonRpcErrorCode::kMethodNotFound));
}

TEST_CASE("Experimental capabilities are passed through during negotiation", "[conformance][capabilities]")
{
  SECTION("server preserves client experimental capability object")
  {
    mcp::Session serverSession;
    serverSession.setRole(mcp::SessionRole::kServer);

    mcp::jsonrpc::Request initializeRequest = makeInitializeRequest(10);
    (*initializeRequest.params)["capabilities"]["experimental"] = mcp::jsonrpc::JsonValue::object();
    (*initializeRequest.params)["capabilities"]["experimental"]["x-foo"] = mcp::jsonrpc::JsonValue::object();
    (*initializeRequest.params)["capabilities"]["experimental"]["x-foo"]["enabled"] = true;

    const mcp::jsonrpc::Response response = serverSession.handleInitializeRequest(initializeRequest);
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response));

    const auto &negotiated = serverSession.negotiatedParameters();
    REQUIRE(negotiated.has_value());
    REQUIRE(negotiated->clientCapabilities().experimental().has_value());
    REQUIRE((*negotiated->clientCapabilities().experimental())["x-foo"]["enabled"].as<bool>());
  }

  SECTION("client preserves server experimental capability object")
  {
    mcp::Session clientSession;
    clientSession.setRole(mcp::SessionRole::kClient);

    REQUIRE_NOTHROW(clientSession.sendRequest("initialize", mcp::jsonrpc::JsonValue::object()));

    mcp::jsonrpc::SuccessResponse initializeResponse;
    initializeResponse.id = std::int64_t {11};
    initializeResponse.result = mcp::jsonrpc::JsonValue::object();
    initializeResponse.result["protocolVersion"] = std::string(mcp::kLatestProtocolVersion);
    initializeResponse.result["capabilities"] = mcp::jsonrpc::JsonValue::object();
    initializeResponse.result["capabilities"]["experimental"] = mcp::jsonrpc::JsonValue::object();
    initializeResponse.result["capabilities"]["experimental"]["x-bar"] = mcp::jsonrpc::JsonValue::object();
    initializeResponse.result["capabilities"]["experimental"]["x-bar"]["rollout"] = "beta";
    initializeResponse.result["serverInfo"] = mcp::jsonrpc::JsonValue::object();
    initializeResponse.result["serverInfo"]["name"] = "conformance-server";
    initializeResponse.result["serverInfo"]["version"] = "1.0.0";

    REQUIRE_NOTHROW(clientSession.handleInitializeResponse(mcp::jsonrpc::Response {initializeResponse}));
    REQUIRE(clientSession.checkCapability("experimental"));

    const auto &negotiated = clientSession.negotiatedParameters();
    REQUIRE(negotiated.has_value());
    REQUIRE(negotiated->serverCapabilities().experimental().has_value());
    REQUIRE((*negotiated->serverCapabilities().experimental())["x-bar"]["rollout"].as<std::string>() == "beta");
  }
}
