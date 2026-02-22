#include <cstdint>
#include <memory>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/sdk/errors.hpp>
#include <mcp/sdk/version.hpp>
#include <mcp/server/server.hpp>

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

auto assertErrorCode(const mcp::jsonrpc::Response &response, mcp::JsonRpcErrorCode expectedCode) -> void
{
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
  const auto &errorResponse = std::get<mcp::jsonrpc::ErrorResponse>(response);
  REQUIRE(errorResponse.error.code == static_cast<std::int32_t>(expectedCode));
}

auto dispatchRequest(mcp::Server &server, const mcp::jsonrpc::Request &request) -> mcp::jsonrpc::Response
{
  return server.handleRequest(mcp::jsonrpc::RequestContext {}, request).get();
}

}  // namespace

TEST_CASE("Server enforces initialize then initialized ordering", "[conformance][lifecycle]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, std::nullopt, mcp::ToolsCapability {}, std::nullopt, std::nullopt);

  const std::shared_ptr<mcp::Server> server = mcp::Server::create(std::move(configuration));

  const mcp::jsonrpc::Response beforeInitializeTools = dispatchRequest(*server, makeRequest(10, "tools/list"));
  assertErrorCode(beforeInitializeTools, mcp::JsonRpcErrorCode::kInvalidRequest);

  const mcp::jsonrpc::Response initializeResponse = dispatchRequest(*server, makeInitializeRequest(11));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initializeResponse));

  const mcp::jsonrpc::Response beforeInitializedTools = dispatchRequest(*server, makeRequest(12, "tools/list"));
  assertErrorCode(beforeInitializedTools, mcp::JsonRpcErrorCode::kInvalidRequest);

  mcp::jsonrpc::Notification initialized;
  initialized.method = "notifications/initialized";
  server->handleNotification(mcp::jsonrpc::RequestContext {}, initialized);

  const mcp::jsonrpc::Response afterInitializedTools = dispatchRequest(*server, makeRequest(13, "tools/list"));
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(afterInitializedTools));
}

TEST_CASE("Initialized notification before initialize does not unlock feature methods", "[conformance][lifecycle]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, std::nullopt, mcp::ToolsCapability {}, std::nullopt, std::nullopt);

  const std::shared_ptr<mcp::Server> server = mcp::Server::create(std::move(configuration));

  mcp::jsonrpc::Notification initialized;
  initialized.method = "notifications/initialized";
  server->handleNotification(mcp::jsonrpc::RequestContext {}, initialized);

  const mcp::jsonrpc::Response toolsResponse = dispatchRequest(*server, makeRequest(21, "tools/list"));
  assertErrorCode(toolsResponse, mcp::JsonRpcErrorCode::kInvalidRequest);
}
