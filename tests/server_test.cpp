#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/errors.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/server.hpp>
#include <mcp/version.hpp>

namespace
{

static constexpr std::int64_t kPingRequestId = 3;
static constexpr std::int64_t kToolsListBeforeInitializeId = 4;
static constexpr std::int64_t kLoggingSetLevelId = 5;
static constexpr std::int64_t kToolsListBeforeInitializedId = 6;
static constexpr std::int64_t kCapabilityGatingRequestId = 7;
static constexpr std::int64_t kUnknownMethodRequestId = 8;
static constexpr std::int64_t kCompletionRequestId = 9;
static constexpr std::int64_t kSetLevelRequestId = 10;

static auto makeReadyResponseFuture(mcp::jsonrpc::Response response) -> std::future<mcp::jsonrpc::Response>
{
  std::promise<mcp::jsonrpc::Response> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

static auto makeInitializeRequest(std::int64_t requestId = 1) -> mcp::jsonrpc::Request
{
  mcp::jsonrpc::Request request;
  request.id = requestId;
  request.method = "initialize";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["protocolVersion"] = std::string(mcp::kLatestProtocolVersion);
  (*request.params)["capabilities"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["clientInfo"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["clientInfo"]["name"] = "server-test-client";
  (*request.params)["clientInfo"]["version"] = "1.0.0";
  return request;
}

static auto dispatchRequest(mcp::Server &server, const mcp::jsonrpc::Request &request) -> mcp::jsonrpc::Response
{
  const mcp::jsonrpc::RequestContext context;
  return server.handleRequest(context, request).get();
}

static auto assertErrorCode(const mcp::jsonrpc::Response &response, mcp::JsonRpcErrorCode expectedCode) -> void
{
  REQUIRE(std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response));
  const auto &errorResponse = std::get<mcp::jsonrpc::ErrorResponse>(response);
  REQUIRE(errorResponse.error.code == static_cast<std::int32_t>(expectedCode));
}

static auto completeInitialization(mcp::Server &server) -> void
{
  const mcp::jsonrpc::Response initializeResponse = dispatchRequest(server, makeInitializeRequest());
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initializeResponse));

  mcp::jsonrpc::Notification initialized;
  initialized.method = "notifications/initialized";
  server.handleNotification(mcp::jsonrpc::RequestContext {}, initialized);
}

}  // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Server initialize result reflects configured capabilities and metadata", "[server][core][initialize]")
{
  mcp::PromptsCapability prompts;
  prompts.listChanged = true;

  mcp::ToolsCapability tools;
  tools.listChanged = true;

  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::ServerCapabilities(mcp::LoggingCapability {}, std::nullopt, prompts, std::nullopt, tools, std::nullopt, std::nullopt);
  configuration.serverInfo = mcp::Implementation("configured-server", "2.3.4");
  configuration.instructions = "Use tools only when needed.";

  auto server = mcp::Server::create(std::move(configuration));

  const mcp::jsonrpc::Response response = dispatchRequest(*server, makeInitializeRequest());
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response));

  const auto &success = std::get<mcp::jsonrpc::SuccessResponse>(response);
  REQUIRE(success.result["capabilities"].contains("logging"));
  REQUIRE(success.result["capabilities"].contains("prompts"));
  REQUIRE(success.result["capabilities"]["prompts"]["listChanged"].as<bool>());
  REQUIRE(success.result["capabilities"].contains("tools"));
  REQUIRE(success.result["capabilities"]["tools"]["listChanged"].as<bool>());
  REQUIRE(success.result["serverInfo"]["name"].as<std::string>() == "configured-server");
  REQUIRE(success.result["serverInfo"]["version"].as<std::string>() == "2.3.4");
  REQUIRE(success.result["instructions"].as<std::string>() == "Use tools only when needed.");

  const auto &negotiated = server->session()->negotiatedParameters();
  REQUIRE(negotiated.has_value());
  if (negotiated.has_value())
  {
    REQUIRE(negotiated->serverCapabilities().hasCapability("logging"));
    REQUIRE(negotiated->serverCapabilities().hasCapability("tools"));
  }
}

TEST_CASE("Server enforces pre-initialization lifecycle rules", "[server][core][lifecycle]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::ServerCapabilities(mcp::LoggingCapability {}, std::nullopt, std::nullopt, std::nullopt, mcp::ToolsCapability {}, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));

  server->registerRequestHandler("tools/list",
                                 [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                 {
                                   mcp::jsonrpc::SuccessResponse success;
                                   success.id = request.id;
                                   success.result = mcp::jsonrpc::JsonValue::object();
                                   success.result["tools"] = mcp::jsonrpc::JsonValue::array();
                                   return makeReadyResponseFuture(mcp::jsonrpc::Response {success});
                                 });

  mcp::jsonrpc::Request pingRequest;
  pingRequest.id = kPingRequestId;
  pingRequest.method = "ping";
  const mcp::jsonrpc::Response pingResponse = dispatchRequest(*server, pingRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(pingResponse));

  mcp::jsonrpc::Request toolsListBeforeInitialize;
  toolsListBeforeInitialize.id = kToolsListBeforeInitializeId;
  toolsListBeforeInitialize.method = "tools/list";
  toolsListBeforeInitialize.params = mcp::jsonrpc::JsonValue::object();
  const mcp::jsonrpc::Response beforeInitializeResponse = dispatchRequest(*server, toolsListBeforeInitialize);
  assertErrorCode(beforeInitializeResponse, mcp::JsonRpcErrorCode::kInvalidRequest);

  const mcp::jsonrpc::Response initializeResponse = dispatchRequest(*server, makeInitializeRequest());
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(initializeResponse));

  mcp::jsonrpc::Request loggingSetLevel;
  loggingSetLevel.id = kLoggingSetLevelId;
  loggingSetLevel.method = "logging/setLevel";
  loggingSetLevel.params = mcp::jsonrpc::JsonValue::object();
  (*loggingSetLevel.params)["level"] = "info";
  const mcp::jsonrpc::Response loggingResponse = dispatchRequest(*server, loggingSetLevel);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(loggingResponse));

  mcp::jsonrpc::Request toolsListBeforeInitialized;
  toolsListBeforeInitialized.id = kToolsListBeforeInitializedId;
  toolsListBeforeInitialized.method = "tools/list";
  toolsListBeforeInitialized.params = mcp::jsonrpc::JsonValue::object();
  const mcp::jsonrpc::Response beforeInitializedResponse = dispatchRequest(*server, toolsListBeforeInitialized);
  assertErrorCode(beforeInitializedResponse, mcp::JsonRpcErrorCode::kInvalidRequest);

  mcp::jsonrpc::Notification initialized;
  initialized.method = "notifications/initialized";
  server->handleNotification(mcp::jsonrpc::RequestContext {}, initialized);

  const mcp::jsonrpc::Response toolsListOperatingResponse = dispatchRequest(*server, toolsListBeforeInitialized);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(toolsListOperatingResponse));
}

TEST_CASE("Server completion enforces a maximum of 100 values", "[server][utilities][completion]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::ServerCapabilities(std::nullopt, mcp::CompletionsCapability {}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));
  server->setCompletionHandler(
    [](const mcp::CompletionRequest &) -> mcp::CompletionResult
    {
      mcp::CompletionResult result;
      result.values.reserve(120);
      for (std::size_t index = 0; index < 120; ++index)
      {
        result.values.push_back("value-" + std::to_string(index));
      }
      result.total = 120;
      result.hasMore = false;
      return result;
    });

  completeInitialization(*server);

  mcp::jsonrpc::Request request;
  request.id = kCompletionRequestId;
  request.method = "completion/complete";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["ref"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["ref"]["type"] = "ref/prompt";
  (*request.params)["ref"]["name"] = "prompt-a";
  (*request.params)["argument"] = mcp::jsonrpc::JsonValue::object();
  (*request.params)["argument"]["name"] = "language";
  (*request.params)["argument"]["value"] = "c";

  const mcp::jsonrpc::Response response = dispatchRequest(*server, request);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response));
  const auto &success = std::get<mcp::jsonrpc::SuccessResponse>(response);
  REQUIRE(success.result.contains("completion"));
  REQUIRE(success.result["completion"]["values"].size() == 100);
  REQUIRE(success.result["completion"]["values"][0].as<std::string>() == "value-0");
  REQUIRE(success.result["completion"]["values"][99].as<std::string>() == "value-99");
  REQUIRE(success.result["completion"]["total"].as<std::size_t>() == 120);
  REQUIRE(success.result["completion"]["hasMore"].as<bool>());
}

TEST_CASE("Server pagination cursors are opaque and endpoint-scoped", "[server][utilities][pagination]")
{
  const mcp::PaginationWindow firstPage = mcp::Server::paginateList(mcp::ListEndpoint::kTools, std::nullopt, 11, 4);
  REQUIRE(firstPage.startIndex == 0);
  REQUIRE(firstPage.endIndex == 4);
  REQUIRE(firstPage.nextCursor.has_value());
  if (firstPage.nextCursor.has_value())
  {
    REQUIRE(*firstPage.nextCursor != "4");
  }

  const mcp::PaginationWindow secondPage = mcp::Server::paginateList(mcp::ListEndpoint::kTools, firstPage.nextCursor, 11, 4);
  REQUIRE(secondPage.startIndex == 4);
  REQUIRE(secondPage.endIndex == 8);
  REQUIRE(secondPage.nextCursor.has_value());

  REQUIRE_THROWS_AS(mcp::Server::paginateList(mcp::ListEndpoint::kResources, firstPage.nextCursor, 11, 4), std::invalid_argument);
  REQUIRE_THROWS_AS(mcp::Server::paginateList(mcp::ListEndpoint::kPrompts, std::optional<std::string> {"invalid-cursor"}, 11, 4), std::invalid_argument);
}

TEST_CASE("Server logging level updates and filters outbound notifications", "[server][utilities][logging]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::ServerCapabilities(mcp::LoggingCapability {}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));

  std::mutex messagesMutex;
  std::vector<mcp::jsonrpc::Message> outboundMessages;
  server->setOutboundMessageSender(
    [&messagesMutex, &outboundMessages](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
    {
      const std::scoped_lock lock(messagesMutex);
      outboundMessages.push_back(std::move(message));
    });

  completeInitialization(*server);

  REQUIRE(server->emitLogMessage(mcp::jsonrpc::RequestContext {}, mcp::LogLevel::kInfo, mcp::jsonrpc::JsonValue("first"), std::string("test")));

  mcp::jsonrpc::Request setLevel;
  setLevel.id = kSetLevelRequestId;
  setLevel.method = "logging/setLevel";
  setLevel.params = mcp::jsonrpc::JsonValue::object();
  (*setLevel.params)["level"] = "error";
  const mcp::jsonrpc::Response setLevelResponse = dispatchRequest(*server, setLevel);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(setLevelResponse));
  REQUIRE(server->logLevel() == mcp::LogLevel::kError);

  REQUIRE_FALSE(server->emitLogMessage(mcp::jsonrpc::RequestContext {}, mcp::LogLevel::kWarning, mcp::jsonrpc::JsonValue("filtered")));
  REQUIRE(server->emitLogMessage(mcp::jsonrpc::RequestContext {}, mcp::LogLevel::kCritical, mcp::jsonrpc::JsonValue::object()));

  std::vector<mcp::jsonrpc::Notification> logNotifications;
  {
    const std::scoped_lock lock(messagesMutex);
    for (const auto &message : outboundMessages)
    {
      if (std::holds_alternative<mcp::jsonrpc::Notification>(message))
      {
        const auto &notification = std::get<mcp::jsonrpc::Notification>(message);
        if (notification.method == "notifications/message")
        {
          logNotifications.push_back(notification);
        }
      }
    }
  }

  REQUIRE(logNotifications.size() == 2);
  REQUIRE(logNotifications[0].params.has_value());
  REQUIRE((*logNotifications[0].params)["level"].as<std::string>() == "info");
  REQUIRE((*logNotifications[0].params)["logger"].as<std::string>() == "test");
  REQUIRE(logNotifications[1].params.has_value());
  REQUIRE((*logNotifications[1].params)["level"].as<std::string>() == "critical");
}

TEST_CASE("Server returns method not found when a feature capability is not declared", "[server][core][capabilities]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::ServerCapabilities(mcp::LoggingCapability {}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));
  server->registerRequestHandler("tools/list",
                                 [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                 {
                                   mcp::jsonrpc::SuccessResponse success;
                                   success.id = request.id;
                                   success.result = mcp::jsonrpc::JsonValue::object();
                                   success.result["tools"] = mcp::jsonrpc::JsonValue::array();
                                   return makeReadyResponseFuture(mcp::jsonrpc::Response {success});
                                 });

  completeInitialization(*server);

  mcp::jsonrpc::Request request;
  request.id = kCapabilityGatingRequestId;
  request.method = "tools/list";
  request.params = mcp::jsonrpc::JsonValue::object();

  const mcp::jsonrpc::Response response = dispatchRequest(*server, request);
  assertErrorCode(response, mcp::JsonRpcErrorCode::kMethodNotFound);
}

TEST_CASE("Server returns method not found for unregistered methods", "[server][core][dispatch]")
{
  auto server = mcp::Server::create();
  completeInitialization(*server);

  mcp::jsonrpc::Request request;
  request.id = kUnknownMethodRequestId;
  request.method = "unknown/method";
  request.params = mcp::jsonrpc::JsonValue::object();

  const mcp::jsonrpc::Response response = dispatchRequest(*server, request);
  assertErrorCode(response, mcp::JsonRpcErrorCode::kMethodNotFound);
}

// NOLINTEND(readability-function-cognitive-complexity)
