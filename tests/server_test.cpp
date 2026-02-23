#include <chrono>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/sdk/errors.hpp>
#include <mcp/sdk/version.hpp>
#include <mcp/server/prompts.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/tools.hpp>

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
static constexpr std::int64_t kToolsListPaginationRequestId = 11;
static constexpr std::int64_t kToolsCallUnknownRequestId = 12;
static constexpr std::int64_t kToolsCallSchemaFailureRequestId = 13;
static constexpr std::int64_t kToolsCallSuccessRequestId = 14;
static constexpr std::int64_t kToolsCallOutputValidationRequestId = 15;
static constexpr std::int64_t kCompletionResourceRequestId = 16;
static constexpr std::int64_t kToolsCallExecutionFailureRequestId = 17;
static constexpr std::int64_t kResourcesListPaginationRequestId = 100;
static constexpr std::int64_t kResourcesReadRequestId = 110;
static constexpr std::int64_t kResourcesMissingReadRequestId = 120;
static constexpr std::int64_t kResourceTemplatesListPaginationRequestId = 130;
static constexpr std::int64_t kResourcesSubscribeRequestId = 140;
static constexpr std::int64_t kResourcesUnsubscribeRequestId = 141;
static constexpr std::int64_t kResourcesSubscribeGatingRequestId = 142;
static constexpr std::int64_t kPromptsListPaginationRequestId = 150;
static constexpr std::int64_t kPromptsGetRequestId = 160;
static constexpr std::int64_t kPromptsGetMissingArgumentRequestId = 161;
static constexpr std::int64_t kPromptsGetUnknownArgumentRequestId = 162;
static constexpr std::int64_t kPromptsGetInvalidArgumentTypeRequestId = 163;

static constexpr std::size_t kToolsListPageSize = 50;
static constexpr std::size_t kResourcesListPageSize = 50;
static constexpr std::size_t kResourceTemplatesListPageSize = 50;
static constexpr std::size_t kPromptsListPageSize = 50;
static constexpr std::size_t kTasksListPageSize = 50;

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

static auto dispatchRequest(mcp::Server &server, const mcp::jsonrpc::Request &request, const mcp::jsonrpc::RequestContext &context = {}) -> mcp::jsonrpc::Response
{
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

static auto makeToolDefinition(std::string name) -> mcp::ToolDefinition
{
  mcp::ToolDefinition definition;
  definition.name = std::move(name);
  definition.description = "test tool";
  definition.inputSchema = mcp::jsonrpc::JsonValue::object();
  definition.inputSchema["type"] = "object";
  definition.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();
  definition.inputSchema["properties"]["value"] = mcp::jsonrpc::JsonValue::object();
  definition.inputSchema["properties"]["value"]["type"] = "string";
  definition.inputSchema["required"] = mcp::jsonrpc::JsonValue::array();
  definition.inputSchema["required"].push_back("value");
  return definition;
}

static auto makeResourceDefinition(std::string uri, std::string name) -> mcp::ResourceDefinition
{
  mcp::ResourceDefinition definition;
  definition.uri = std::move(uri);
  definition.name = std::move(name);
  definition.description = "test resource";
  definition.mimeType = "text/plain";
  return definition;
}

static auto makeResourceTemplate(std::string uriTemplate, std::string name) -> mcp::ResourceTemplateDefinition
{
  mcp::ResourceTemplateDefinition definition;
  definition.uriTemplate = std::move(uriTemplate);
  definition.name = std::move(name);
  definition.description = "test template";
  return definition;
}

static auto makePromptDefinition(std::string name) -> mcp::PromptDefinition
{
  mcp::PromptDefinition definition;
  definition.name = std::move(name);
  definition.description = "test prompt";

  mcp::PromptArgumentDefinition argument;
  argument.name = "topic";
  argument.description = "Topic to explain";
  argument.required = true;

  definition.arguments.push_back(std::move(argument));
  return definition;
}

}  // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Server initialize result reflects configured capabilities and metadata", "[server][core][initialize]")
{
  mcp::lifecycle::session::PromptsCapability prompts;
  prompts.listChanged = true;

  mcp::lifecycle::session::ToolsCapability tools;
  tools.listChanged = true;

  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(mcp::lifecycle::session::LoggingCapability {}, std::nullopt, prompts, std::nullopt, tools, std::nullopt, std::nullopt);
  configuration.serverInfo = mcp::lifecycle::session::Implementation("configured-server", "2.3.4");
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
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(mcp::lifecycle::session::LoggingCapability {}, std::nullopt, std::nullopt, std::nullopt, mcp::lifecycle::session::ToolsCapability {}, std::nullopt, std::nullopt);

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

TEST_CASE("Server completion enforces max values and preserves reference semantics", "[server][utilities][completion]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, mcp::lifecycle::session::CompletionsCapability {}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));

  std::vector<mcp::CompletionRequest> observedRequests;
  server->setCompletionHandler(
    [&observedRequests](const mcp::CompletionRequest &request) -> mcp::CompletionResult
    {
      observedRequests.push_back(request);

      mcp::CompletionResult result;
      if (request.referenceType == mcp::CompletionReferenceType::kPrompt)
      {
        result.values = {"prompt-suggestion"};
        result.total = 1;
        result.hasMore = false;
        return result;
      }

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

  mcp::jsonrpc::Request promptRequest;
  promptRequest.id = kCompletionRequestId;
  promptRequest.method = "completion/complete";
  promptRequest.params = mcp::jsonrpc::JsonValue::object();
  (*promptRequest.params)["ref"] = mcp::jsonrpc::JsonValue::object();
  (*promptRequest.params)["ref"]["type"] = "ref/prompt";
  (*promptRequest.params)["ref"]["name"] = "prompt-a";
  (*promptRequest.params)["argument"] = mcp::jsonrpc::JsonValue::object();
  (*promptRequest.params)["argument"]["name"] = "language";
  (*promptRequest.params)["argument"]["value"] = "c";
  (*promptRequest.params)["context"] = mcp::jsonrpc::JsonValue::object();
  (*promptRequest.params)["context"]["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*promptRequest.params)["context"]["arguments"]["framework"] = "catch2";

  const mcp::jsonrpc::Response promptResponse = dispatchRequest(*server, promptRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(promptResponse));
  const auto &promptSuccess = std::get<mcp::jsonrpc::SuccessResponse>(promptResponse);
  REQUIRE(promptSuccess.result.contains("completion"));
  REQUIRE(promptSuccess.result["completion"]["values"].size() == 1);
  REQUIRE(promptSuccess.result["completion"]["values"][0].as<std::string>() == "prompt-suggestion");
  REQUIRE(promptSuccess.result["completion"]["total"].as<std::size_t>() == 1);
  REQUIRE_FALSE(promptSuccess.result["completion"]["hasMore"].as<bool>());

  mcp::jsonrpc::Request resourceRequest;
  resourceRequest.id = kCompletionResourceRequestId;
  resourceRequest.method = "completion/complete";
  resourceRequest.params = mcp::jsonrpc::JsonValue::object();
  (*resourceRequest.params)["ref"] = mcp::jsonrpc::JsonValue::object();
  (*resourceRequest.params)["ref"]["type"] = "ref/resource";
  (*resourceRequest.params)["ref"]["uri"] = "resource://template/{id}";
  (*resourceRequest.params)["argument"] = mcp::jsonrpc::JsonValue::object();
  (*resourceRequest.params)["argument"]["name"] = "id";
  (*resourceRequest.params)["argument"]["value"] = "ab";

  const mcp::jsonrpc::Response resourceResponse = dispatchRequest(*server, resourceRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(resourceResponse));
  const auto &resourceSuccess = std::get<mcp::jsonrpc::SuccessResponse>(resourceResponse);
  REQUIRE(resourceSuccess.result.contains("completion"));
  REQUIRE(resourceSuccess.result["completion"]["values"].size() == 100);
  REQUIRE(resourceSuccess.result["completion"]["values"][0].as<std::string>() == "value-0");
  REQUIRE(resourceSuccess.result["completion"]["values"][99].as<std::string>() == "value-99");
  REQUIRE(resourceSuccess.result["completion"]["total"].as<std::size_t>() == 120);
  REQUIRE(resourceSuccess.result["completion"]["hasMore"].as<bool>());

  REQUIRE(observedRequests.size() == 2);
  REQUIRE(observedRequests[0].referenceType == mcp::CompletionReferenceType::kPrompt);
  REQUIRE(observedRequests[0].referenceValue == "prompt-a");
  REQUIRE(observedRequests[0].argumentName == "language");
  REQUIRE(observedRequests[0].argumentValue == "c");
  REQUIRE(observedRequests[0].contextArguments.has_value());
  if (observedRequests[0].contextArguments.has_value())
  {
    REQUIRE((*observedRequests[0].contextArguments)["framework"].as<std::string>() == "catch2");
  }

  REQUIRE(observedRequests[1].referenceType == mcp::CompletionReferenceType::kResource);
  REQUIRE(observedRequests[1].referenceValue == "resource://template/{id}");
  REQUIRE(observedRequests[1].argumentName == "id");
  REQUIRE(observedRequests[1].argumentValue == "ab");
  REQUIRE_FALSE(observedRequests[1].contextArguments.has_value());
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

TEST_CASE("Server logging/setLevel is capability-gated", "[server][utilities][logging]")
{
  auto server = mcp::Server::create();
  completeInitialization(*server);

  mcp::jsonrpc::Request setLevel;
  setLevel.id = kSetLevelRequestId;
  setLevel.method = "logging/setLevel";
  setLevel.params = mcp::jsonrpc::JsonValue::object();
  (*setLevel.params)["level"] = "error";

  const mcp::jsonrpc::Response setLevelResponse = dispatchRequest(*server, setLevel);
  assertErrorCode(setLevelResponse, mcp::JsonRpcErrorCode::kMethodNotFound);

  REQUIRE_FALSE(server->emitLogMessage(mcp::jsonrpc::RequestContext {}, mcp::LogLevel::kEmergency, mcp::jsonrpc::JsonValue("hidden")));
}

TEST_CASE("Server logging level updates and filters outbound notifications", "[server][utilities][logging]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(mcp::lifecycle::session::LoggingCapability {}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt);

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
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(mcp::lifecycle::session::LoggingCapability {}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt);

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

TEST_CASE("Server tools list supports cursor pagination", "[server][tools][list]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, std::nullopt, mcp::lifecycle::session::ToolsCapability {}, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));
  for (std::size_t index = 0; index < kToolsListPageSize + 5; ++index)
  {
    server->registerTool(makeToolDefinition("tool-" + std::to_string(index)),
                         [](const mcp::ToolCallContext &) -> mcp::CallToolResult
                         {
                           mcp::CallToolResult result;
                           result.content = mcp::jsonrpc::JsonValue::array();
                           return result;
                         });
  }

  completeInitialization(*server);

  mcp::jsonrpc::Request firstPageRequest;
  firstPageRequest.id = kToolsListPaginationRequestId;
  firstPageRequest.method = "tools/list";
  firstPageRequest.params = mcp::jsonrpc::JsonValue::object();

  const mcp::jsonrpc::Response firstPageResponse = dispatchRequest(*server, firstPageRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(firstPageResponse));
  const auto &firstPage = std::get<mcp::jsonrpc::SuccessResponse>(firstPageResponse);
  REQUIRE(firstPage.result["tools"].size() == kToolsListPageSize);
  REQUIRE(firstPage.result.contains("nextCursor"));

  mcp::jsonrpc::Request secondPageRequest;
  secondPageRequest.id = kToolsListPaginationRequestId + 1;
  secondPageRequest.method = "tools/list";
  secondPageRequest.params = mcp::jsonrpc::JsonValue::object();
  (*secondPageRequest.params)["cursor"] = firstPage.result["nextCursor"];

  const mcp::jsonrpc::Response secondPageResponse = dispatchRequest(*server, secondPageRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(secondPageResponse));
  const auto &secondPage = std::get<mcp::jsonrpc::SuccessResponse>(secondPageResponse);
  REQUIRE(secondPage.result["tools"].size() == 5);
  REQUIRE_FALSE(secondPage.result.contains("nextCursor"));

  mcp::jsonrpc::Request invalidCursorRequest;
  invalidCursorRequest.id = kToolsListPaginationRequestId + 2;
  invalidCursorRequest.method = "tools/list";
  invalidCursorRequest.params = mcp::jsonrpc::JsonValue::object();
  (*invalidCursorRequest.params)["cursor"] = "not-a-valid-cursor";

  const mcp::jsonrpc::Response invalidCursorResponse = dispatchRequest(*server, invalidCursorRequest);
  assertErrorCode(invalidCursorResponse, mcp::JsonRpcErrorCode::kInvalidParams);
}

TEST_CASE("Server tools call differentiates unknown tool errors from input schema failures", "[server][tools][call]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, std::nullopt, mcp::lifecycle::session::ToolsCapability {}, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));

  std::size_t invocationCount = 0;
  server->registerTool(makeToolDefinition("echo"),
                       [&invocationCount](const mcp::ToolCallContext &context) -> mcp::CallToolResult
                       {
                         ++invocationCount;
                         mcp::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         mcp::jsonrpc::JsonValue text = mcp::jsonrpc::JsonValue::object();
                         text["type"] = "text";
                         text["text"] = context.arguments["value"].as<std::string>();
                         result.content.push_back(std::move(text));
                         return result;
                       });

  server->registerTool(makeToolDefinition("throws"), [](const mcp::ToolCallContext &) -> mcp::CallToolResult { throw std::runtime_error("tool blew up"); });

  completeInitialization(*server);

  mcp::jsonrpc::Request unknownToolRequest;
  unknownToolRequest.id = kToolsCallUnknownRequestId;
  unknownToolRequest.method = "tools/call";
  unknownToolRequest.params = mcp::jsonrpc::JsonValue::object();
  (*unknownToolRequest.params)["name"] = "missing";

  const mcp::jsonrpc::Response unknownToolResponse = dispatchRequest(*server, unknownToolRequest);
  assertErrorCode(unknownToolResponse, mcp::JsonRpcErrorCode::kInvalidParams);

  mcp::jsonrpc::Request schemaFailureRequest;
  schemaFailureRequest.id = kToolsCallSchemaFailureRequestId;
  schemaFailureRequest.method = "tools/call";
  schemaFailureRequest.params = mcp::jsonrpc::JsonValue::object();
  (*schemaFailureRequest.params)["name"] = "echo";
  (*schemaFailureRequest.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*schemaFailureRequest.params)["arguments"]["value"] = 42;

  const mcp::jsonrpc::Response schemaFailureResponse = dispatchRequest(*server, schemaFailureRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(schemaFailureResponse));
  const auto &schemaFailure = std::get<mcp::jsonrpc::SuccessResponse>(schemaFailureResponse);
  REQUIRE(schemaFailure.result["isError"].as<bool>());
  REQUIRE(invocationCount == 0);

  mcp::jsonrpc::Request executionFailureRequest;
  executionFailureRequest.id = kToolsCallExecutionFailureRequestId;
  executionFailureRequest.method = "tools/call";
  executionFailureRequest.params = mcp::jsonrpc::JsonValue::object();
  (*executionFailureRequest.params)["name"] = "throws";
  (*executionFailureRequest.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*executionFailureRequest.params)["arguments"]["value"] = "input";

  const mcp::jsonrpc::Response executionFailureResponse = dispatchRequest(*server, executionFailureRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(executionFailureResponse));
  const auto &executionFailure = std::get<mcp::jsonrpc::SuccessResponse>(executionFailureResponse);
  REQUIRE(executionFailure.result["isError"].as<bool>());
  REQUIRE(executionFailure.result["content"].is_array());
  REQUIRE(executionFailure.result["content"][0]["type"].as<std::string>() == "text");
  REQUIRE(executionFailure.result["content"][0]["text"].as<std::string>() == "Tool execution failed: tool blew up");
}

TEST_CASE("Server tools call validates structured output when output schema is declared", "[server][tools][output]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, std::nullopt, mcp::lifecycle::session::ToolsCapability {}, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));

  mcp::ToolDefinition validTool = makeToolDefinition("counter");
  validTool.outputSchema = mcp::jsonrpc::JsonValue::object();
  (*validTool.outputSchema)["type"] = "object";
  (*validTool.outputSchema)["properties"] = mcp::jsonrpc::JsonValue::object();
  (*validTool.outputSchema)["properties"]["count"] = mcp::jsonrpc::JsonValue::object();
  (*validTool.outputSchema)["properties"]["count"]["type"] = "number";
  (*validTool.outputSchema)["required"] = mcp::jsonrpc::JsonValue::array();
  (*validTool.outputSchema)["required"].push_back("count");

  server->registerTool(validTool,
                       [](const mcp::ToolCallContext &) -> mcp::CallToolResult
                       {
                         mcp::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         mcp::jsonrpc::JsonValue text = mcp::jsonrpc::JsonValue::object();
                         text["type"] = "text";
                         text["text"] = "ok";
                         result.content.push_back(std::move(text));
                         result.structuredContent = mcp::jsonrpc::JsonValue::object();
                         (*result.structuredContent)["count"] = 7;
                         return result;
                       });

  mcp::ToolDefinition invalidOutputTool = makeToolDefinition("broken-counter");
  invalidOutputTool.outputSchema = validTool.outputSchema;
  server->registerTool(invalidOutputTool,
                       [](const mcp::ToolCallContext &) -> mcp::CallToolResult
                       {
                         mcp::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         mcp::jsonrpc::JsonValue text = mcp::jsonrpc::JsonValue::object();
                         text["type"] = "text";
                         text["text"] = "broken";
                         result.content.push_back(std::move(text));
                         result.structuredContent = mcp::jsonrpc::JsonValue::object();
                         (*result.structuredContent)["count"] = "not-a-number";
                         return result;
                       });

  completeInitialization(*server);

  mcp::jsonrpc::Request successCall;
  successCall.id = kToolsCallSuccessRequestId;
  successCall.method = "tools/call";
  successCall.params = mcp::jsonrpc::JsonValue::object();
  (*successCall.params)["name"] = "counter";
  (*successCall.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*successCall.params)["arguments"]["value"] = "a";

  const mcp::jsonrpc::Response successResponse = dispatchRequest(*server, successCall);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(successResponse));
  const auto &success = std::get<mcp::jsonrpc::SuccessResponse>(successResponse);
  REQUIRE(success.result.contains("structuredContent"));
  REQUIRE(success.result["structuredContent"]["count"].as<std::int64_t>() == 7);
  REQUIRE_FALSE(success.result.contains("isError"));

  mcp::jsonrpc::Request invalidOutputCall;
  invalidOutputCall.id = kToolsCallOutputValidationRequestId;
  invalidOutputCall.method = "tools/call";
  invalidOutputCall.params = mcp::jsonrpc::JsonValue::object();
  (*invalidOutputCall.params)["name"] = "broken-counter";
  (*invalidOutputCall.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*invalidOutputCall.params)["arguments"]["value"] = "a";

  const mcp::jsonrpc::Response invalidOutputResponse = dispatchRequest(*server, invalidOutputCall);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(invalidOutputResponse));
  const auto &invalidOutput = std::get<mcp::jsonrpc::SuccessResponse>(invalidOutputResponse);
  REQUIRE(invalidOutput.result["isError"].as<bool>());
  REQUIRE_FALSE(invalidOutput.result.contains("structuredContent"));
}

TEST_CASE("Server tools/call task augmentation returns deferred result and enforces auth binding", "[server][tasks][tools]")
{
  mcp::lifecycle::session::ToolsCapability toolsCapability;

  mcp::lifecycle::session::TasksCapability tasksCapability;
  tasksCapability.list = true;
  tasksCapability.cancel = true;
  tasksCapability.toolsCall = true;

  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, std::nullopt, toolsCapability, tasksCapability, std::nullopt);
  configuration.emitTaskStatusNotifications = true;

  auto server = mcp::Server::create(std::move(configuration));

  mcp::ToolDefinition toolDefinition = makeToolDefinition("task-echo");
  toolDefinition.execution = mcp::jsonrpc::JsonValue::object();
  (*toolDefinition.execution)["taskSupport"] = "optional";
  server->registerTool(toolDefinition,
                       [](const mcp::ToolCallContext &context) -> mcp::CallToolResult
                       {
                         mcp::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();

                         mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
                         content["type"] = "text";
                         content["text"] = "echo:" + context.arguments["value"].as<std::string>();
                         result.content.push_back(std::move(content));
                         return result;
                       });

  std::mutex messagesMutex;
  std::vector<mcp::jsonrpc::Message> outboundMessages;
  server->setOutboundMessageSender(
    [&messagesMutex, &outboundMessages](const mcp::jsonrpc::RequestContext &, mcp::jsonrpc::Message message) -> void
    {
      const std::scoped_lock lock(messagesMutex);
      outboundMessages.push_back(std::move(message));
    });

  completeInitialization(*server);

  mcp::jsonrpc::RequestContext authA;
  authA.authContext = "auth-a";

  mcp::jsonrpc::Request taskToolCall;
  taskToolCall.id = std::int64_t {17};
  taskToolCall.method = "tools/call";
  taskToolCall.params = mcp::jsonrpc::JsonValue::object();
  (*taskToolCall.params)["name"] = "task-echo";
  (*taskToolCall.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*taskToolCall.params)["arguments"]["value"] = "payload";
  (*taskToolCall.params)["task"] = mcp::jsonrpc::JsonValue::object();
  (*taskToolCall.params)["task"]["ttl"] = std::int64_t {5000};

  const mcp::jsonrpc::Response taskCreateResponse = dispatchRequest(*server, taskToolCall, authA);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(taskCreateResponse));
  const auto &createdTask = std::get<mcp::jsonrpc::SuccessResponse>(taskCreateResponse);
  REQUIRE(createdTask.result.contains("task"));
  REQUIRE(createdTask.result["task"]["status"].as<std::string>() == "working");
  REQUIRE(createdTask.result["task"]["ttl"].as<std::int64_t>() == 5000);
  const std::string taskId = createdTask.result["task"]["taskId"].as<std::string>();
  REQUIRE_FALSE(taskId.empty());

  mcp::jsonrpc::Request resultRequest;
  resultRequest.id = std::int64_t {18};
  resultRequest.method = "tasks/result";
  resultRequest.params = mcp::jsonrpc::JsonValue::object();
  (*resultRequest.params)["taskId"] = taskId;

  const mcp::jsonrpc::Response taskResultResponse = dispatchRequest(*server, resultRequest, authA);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(taskResultResponse));
  const auto &taskResult = std::get<mcp::jsonrpc::SuccessResponse>(taskResultResponse);
  REQUIRE(taskResult.result["content"][0]["text"].as<std::string>() == "echo:payload");
  REQUIRE(taskResult.result.contains("_meta"));
  REQUIRE(taskResult.result["_meta"]["io.modelcontextprotocol/related-task"]["taskId"].as<std::string>() == taskId);

  mcp::jsonrpc::RequestContext authB;
  authB.authContext = "auth-b";

  mcp::jsonrpc::Request deniedGet;
  deniedGet.id = std::int64_t {19};
  deniedGet.method = "tasks/get";
  deniedGet.params = mcp::jsonrpc::JsonValue::object();
  (*deniedGet.params)["taskId"] = taskId;

  const mcp::jsonrpc::Response deniedGetResponse = dispatchRequest(*server, deniedGet, authB);
  assertErrorCode(deniedGetResponse, mcp::JsonRpcErrorCode::kInvalidParams);

  bool sawStatusNotification = false;
  {
    const std::scoped_lock lock(messagesMutex);
    for (const auto &message : outboundMessages)
    {
      if (!std::holds_alternative<mcp::jsonrpc::Notification>(message))
      {
        continue;
      }

      const auto &notification = std::get<mcp::jsonrpc::Notification>(message);
      if (notification.method == "notifications/tasks/status" && notification.params.has_value() && (*notification.params)["taskId"].as<std::string>() == taskId)
      {
        sawStatusNotification = true;
      }
    }
  }

  REQUIRE(sawStatusNotification);
}

TEST_CASE("Server teardown is safe with in-flight task-augmented tools worker", "[server][tasks][tools][teardown]")
{
  mcp::lifecycle::session::ToolsCapability toolsCapability;

  mcp::lifecycle::session::TasksCapability tasksCapability;
  tasksCapability.toolsCall = true;

  auto taskStore = std::make_shared<mcp::util::InMemoryTaskStore>();

  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, std::nullopt, toolsCapability, tasksCapability, std::nullopt);
  configuration.emitTaskStatusNotifications = true;
  configuration.taskStore = taskStore;

  auto server = mcp::Server::create(std::move(configuration));

  mcp::ToolDefinition toolDefinition = makeToolDefinition("teardown-task-tool");
  toolDefinition.execution = mcp::jsonrpc::JsonValue::object();
  (*toolDefinition.execution)["taskSupport"] = "optional";

  std::promise<void> handlerEnteredPromise;
  auto handlerEnteredFuture = handlerEnteredPromise.get_future();
  std::promise<void> releaseHandlerPromise;
  auto releaseHandlerFuture = releaseHandlerPromise.get_future();
  std::promise<void> handlerFinishedPromise;
  auto handlerFinishedFuture = handlerFinishedPromise.get_future();

  server->registerTool(toolDefinition,
                       [&handlerEnteredPromise, &releaseHandlerFuture, &handlerFinishedPromise](const mcp::ToolCallContext &) -> mcp::CallToolResult
                       {
                         handlerEnteredPromise.set_value();
                         releaseHandlerFuture.wait();

                         mcp::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
                         content["type"] = "text";
                         content["text"] = "done";
                         result.content.push_back(std::move(content));

                         handlerFinishedPromise.set_value();
                         return result;
                       });

  completeInitialization(*server);

  mcp::jsonrpc::Request createRequest;
  createRequest.id = std::int64_t {9201};
  createRequest.method = "tools/call";
  createRequest.params = mcp::jsonrpc::JsonValue::object();
  (*createRequest.params)["name"] = "teardown-task-tool";
  (*createRequest.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*createRequest.params)["arguments"]["value"] = "payload";
  (*createRequest.params)["task"] = mcp::jsonrpc::JsonValue::object();
  (*createRequest.params)["task"]["ttl"] = std::int64_t {2000};

  const mcp::jsonrpc::Response createResponse = dispatchRequest(*server, createRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(createResponse));
  const auto &createSuccess = std::get<mcp::jsonrpc::SuccessResponse>(createResponse);
  REQUIRE(createSuccess.result.contains("task"));
  const std::string taskId = createSuccess.result["task"]["taskId"].as<std::string>();
  REQUIRE_FALSE(taskId.empty());

  REQUIRE(handlerEnteredFuture.wait_for(std::chrono::seconds(1)) == std::future_status::ready);

  server.reset();

  releaseHandlerPromise.set_value();
  REQUIRE(handlerFinishedFuture.wait_for(std::chrono::seconds(1)) == std::future_status::ready);

  const mcp::util::TaskTerminalResult terminalResult = taskStore->waitForTaskTerminal(taskId, std::nullopt);
  REQUIRE(terminalResult.error == mcp::util::TaskStoreError::kNone);
  REQUIRE(terminalResult.task.taskId == taskId);
  REQUIRE(terminalResult.task.status == mcp::util::TaskStatus::kCompleted);
  REQUIRE(terminalResult.result.has_value());
}

TEST_CASE("Server tools/call enforces tool-level task support negotiation", "[server][tasks][negotiation]")
{
  mcp::lifecycle::session::ToolsCapability toolsCapability;

  mcp::lifecycle::session::TasksCapability tasksCapability;
  tasksCapability.toolsCall = true;

  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, std::nullopt, toolsCapability, tasksCapability, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));

  mcp::ToolDefinition requiredTaskTool = makeToolDefinition("requires-task");
  requiredTaskTool.execution = mcp::jsonrpc::JsonValue::object();
  (*requiredTaskTool.execution)["taskSupport"] = "required";
  server->registerTool(requiredTaskTool,
                       [](const mcp::ToolCallContext &) -> mcp::CallToolResult
                       {
                         mcp::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         return result;
                       });

  mcp::ToolDefinition forbiddenTaskTool = makeToolDefinition("forbids-task");
  forbiddenTaskTool.execution = mcp::jsonrpc::JsonValue::object();
  (*forbiddenTaskTool.execution)["taskSupport"] = "forbidden";
  server->registerTool(forbiddenTaskTool,
                       [](const mcp::ToolCallContext &) -> mcp::CallToolResult
                       {
                         mcp::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         return result;
                       });

  completeInitialization(*server);

  mcp::jsonrpc::Request missingTaskAugmentation;
  missingTaskAugmentation.id = std::int64_t {20};
  missingTaskAugmentation.method = "tools/call";
  missingTaskAugmentation.params = mcp::jsonrpc::JsonValue::object();
  (*missingTaskAugmentation.params)["name"] = "requires-task";
  (*missingTaskAugmentation.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*missingTaskAugmentation.params)["arguments"]["value"] = "x";

  const mcp::jsonrpc::Response missingTaskResponse = dispatchRequest(*server, missingTaskAugmentation);
  assertErrorCode(missingTaskResponse, mcp::JsonRpcErrorCode::kMethodNotFound);

  mcp::jsonrpc::Request forbiddenTaskAugmentation;
  forbiddenTaskAugmentation.id = std::int64_t {21};
  forbiddenTaskAugmentation.method = "tools/call";
  forbiddenTaskAugmentation.params = mcp::jsonrpc::JsonValue::object();
  (*forbiddenTaskAugmentation.params)["name"] = "forbids-task";
  (*forbiddenTaskAugmentation.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*forbiddenTaskAugmentation.params)["arguments"]["value"] = "x";
  (*forbiddenTaskAugmentation.params)["task"] = mcp::jsonrpc::JsonValue::object();

  const mcp::jsonrpc::Response forbiddenTaskResponse = dispatchRequest(*server, forbiddenTaskAugmentation);
  assertErrorCode(forbiddenTaskResponse, mcp::JsonRpcErrorCode::kMethodNotFound);
}

TEST_CASE("Server tasks/list and tasks/cancel are gated by sub-capabilities", "[server][tasks][capabilities]")
{
  mcp::lifecycle::session::ToolsCapability toolsCapability;

  mcp::lifecycle::session::TasksCapability tasksCapability;
  tasksCapability.list = false;
  tasksCapability.cancel = false;
  tasksCapability.toolsCall = true;

  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, std::nullopt, toolsCapability, tasksCapability, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));

  mcp::ToolDefinition toolDefinition = makeToolDefinition("task-tool");
  toolDefinition.execution = mcp::jsonrpc::JsonValue::object();
  (*toolDefinition.execution)["taskSupport"] = "optional";
  server->registerTool(toolDefinition,
                       [](const mcp::ToolCallContext &) -> mcp::CallToolResult
                       {
                         mcp::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         return result;
                       });

  completeInitialization(*server);

  mcp::jsonrpc::Request createRequest;
  createRequest.id = std::int64_t {22};
  createRequest.method = "tools/call";
  createRequest.params = mcp::jsonrpc::JsonValue::object();
  (*createRequest.params)["name"] = "task-tool";
  (*createRequest.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*createRequest.params)["arguments"]["value"] = "x";
  (*createRequest.params)["task"] = mcp::jsonrpc::JsonValue::object();

  const mcp::jsonrpc::Response createResponse = dispatchRequest(*server, createRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(createResponse));
  const std::string taskId = std::get<mcp::jsonrpc::SuccessResponse>(createResponse).result["task"]["taskId"].as<std::string>();

  mcp::jsonrpc::Request listRequest;
  listRequest.id = std::int64_t {23};
  listRequest.method = "tasks/list";
  listRequest.params = mcp::jsonrpc::JsonValue::object();
  const mcp::jsonrpc::Response listResponse = dispatchRequest(*server, listRequest);
  assertErrorCode(listResponse, mcp::JsonRpcErrorCode::kMethodNotFound);

  mcp::jsonrpc::Request cancelRequest;
  cancelRequest.id = std::int64_t {24};
  cancelRequest.method = "tasks/cancel";
  cancelRequest.params = mcp::jsonrpc::JsonValue::object();
  (*cancelRequest.params)["taskId"] = taskId;
  const mcp::jsonrpc::Response cancelResponse = dispatchRequest(*server, cancelRequest);
  assertErrorCode(cancelResponse, mcp::JsonRpcErrorCode::kMethodNotFound);

  mcp::jsonrpc::Request getRequest;
  getRequest.id = std::int64_t {25};
  getRequest.method = "tasks/get";
  getRequest.params = mcp::jsonrpc::JsonValue::object();
  (*getRequest.params)["taskId"] = taskId;
  const mcp::jsonrpc::Response getResponse = dispatchRequest(*server, getRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(getResponse));

  mcp::jsonrpc::Request resultRequest;
  resultRequest.id = std::int64_t {26};
  resultRequest.method = "tasks/result";
  resultRequest.params = mcp::jsonrpc::JsonValue::object();
  (*resultRequest.params)["taskId"] = taskId;
  const mcp::jsonrpc::Response resultResponse = dispatchRequest(*server, resultRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(resultResponse));
}

TEST_CASE("Server emits tools list_changed notifications when enabled", "[server][tools][notifications]")
{
  mcp::lifecycle::session::ToolsCapability toolsCapability;
  toolsCapability.listChanged = true;

  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, std::nullopt, toolsCapability, std::nullopt, std::nullopt);

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

  server->registerTool(makeToolDefinition("notify-tool"),
                       [](const mcp::ToolCallContext &) -> mcp::CallToolResult
                       {
                         mcp::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         return result;
                       });
  REQUIRE(server->unregisterTool("notify-tool"));

  std::size_t listChangedNotifications = 0;
  {
    const std::scoped_lock lock(messagesMutex);
    for (const auto &message : outboundMessages)
    {
      if (std::holds_alternative<mcp::jsonrpc::Notification>(message))
      {
        const auto &notification = std::get<mcp::jsonrpc::Notification>(message);
        if (notification.method == "notifications/tools/list_changed")
        {
          ++listChangedNotifications;
        }
      }
    }
  }

  REQUIRE(listChangedNotifications == 2);
}

TEST_CASE("Server resources list/read/templates support pagination and blob encoding", "[server][resources][core]")
{
  mcp::lifecycle::session::ResourcesCapability resourcesCapability;
  resourcesCapability.subscribe = true;
  resourcesCapability.listChanged = true;

  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, resourcesCapability, std::nullopt, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));

  for (std::size_t index = 0; index < kResourcesListPageSize + 5; ++index)
  {
    const std::string uri = "resource://item-" + std::to_string(index);
    server->registerResource(makeResourceDefinition(uri, "item-" + std::to_string(index)),
                             [uri](const mcp::ResourceReadContext &) -> std::vector<mcp::ResourceContent>
                             {
                               return {
                                 mcp::ResourceContent::text(uri, "text-content", std::string("text/plain")),
                               };
                             });
  }

  server->registerResource(makeResourceDefinition("resource://blob", "blob-resource"),
                           [](const mcp::ResourceReadContext &) -> std::vector<mcp::ResourceContent>
                           {
                             return {
                               mcp::ResourceContent::blobBytes("resource://blob", std::vector<std::uint8_t> {0x01, 0x02, 0x03, 0x04}, std::string("application/octet-stream")),
                             };
                           });

  for (std::size_t index = 0; index < kResourceTemplatesListPageSize + 5; ++index)
  {
    server->registerResourceTemplate(makeResourceTemplate("resource://template/{id-" + std::to_string(index) + "}", "template-" + std::to_string(index)));
  }

  completeInitialization(*server);

  mcp::jsonrpc::Request listFirstPage;
  listFirstPage.id = kResourcesListPaginationRequestId;
  listFirstPage.method = "resources/list";
  listFirstPage.params = mcp::jsonrpc::JsonValue::object();

  const mcp::jsonrpc::Response firstListResponse = dispatchRequest(*server, listFirstPage);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(firstListResponse));
  const auto &firstList = std::get<mcp::jsonrpc::SuccessResponse>(firstListResponse);
  REQUIRE(firstList.result["resources"].size() == kResourcesListPageSize);
  REQUIRE(firstList.result.contains("nextCursor"));

  mcp::jsonrpc::Request listSecondPage;
  listSecondPage.id = kResourcesListPaginationRequestId + 1;
  listSecondPage.method = "resources/list";
  listSecondPage.params = mcp::jsonrpc::JsonValue::object();
  (*listSecondPage.params)["cursor"] = firstList.result["nextCursor"];

  const mcp::jsonrpc::Response secondListResponse = dispatchRequest(*server, listSecondPage);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(secondListResponse));
  const auto &secondList = std::get<mcp::jsonrpc::SuccessResponse>(secondListResponse);
  REQUIRE(secondList.result["resources"].size() == 6);
  REQUIRE_FALSE(secondList.result.contains("nextCursor"));

  mcp::jsonrpc::Request readTextRequest;
  readTextRequest.id = kResourcesReadRequestId;
  readTextRequest.method = "resources/read";
  readTextRequest.params = mcp::jsonrpc::JsonValue::object();
  (*readTextRequest.params)["uri"] = "resource://item-0";

  const mcp::jsonrpc::Response readTextResponse = dispatchRequest(*server, readTextRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(readTextResponse));
  const auto &readText = std::get<mcp::jsonrpc::SuccessResponse>(readTextResponse);
  REQUIRE(readText.result["contents"].size() == 1);
  REQUIRE(readText.result["contents"][0]["uri"].as<std::string>() == "resource://item-0");
  REQUIRE(readText.result["contents"][0]["text"].as<std::string>() == "text-content");

  mcp::jsonrpc::Request readBlobRequest;
  readBlobRequest.id = kResourcesReadRequestId + 1;
  readBlobRequest.method = "resources/read";
  readBlobRequest.params = mcp::jsonrpc::JsonValue::object();
  (*readBlobRequest.params)["uri"] = "resource://blob";

  const mcp::jsonrpc::Response readBlobResponse = dispatchRequest(*server, readBlobRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(readBlobResponse));
  const auto &readBlob = std::get<mcp::jsonrpc::SuccessResponse>(readBlobResponse);
  REQUIRE(readBlob.result["contents"].size() == 1);
  REQUIRE(readBlob.result["contents"][0]["uri"].as<std::string>() == "resource://blob");
  REQUIRE(readBlob.result["contents"][0]["blob"].as<std::string>() == "AQIDBA==");
  REQUIRE(readBlob.result["contents"][0]["mimeType"].as<std::string>() == "application/octet-stream");

  mcp::jsonrpc::Request templatesFirstPage;
  templatesFirstPage.id = kResourceTemplatesListPaginationRequestId;
  templatesFirstPage.method = "resources/templates/list";
  templatesFirstPage.params = mcp::jsonrpc::JsonValue::object();

  const mcp::jsonrpc::Response firstTemplateResponse = dispatchRequest(*server, templatesFirstPage);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(firstTemplateResponse));
  const auto &firstTemplatePage = std::get<mcp::jsonrpc::SuccessResponse>(firstTemplateResponse);
  REQUIRE(firstTemplatePage.result["resourceTemplates"].size() == kResourceTemplatesListPageSize);
  REQUIRE(firstTemplatePage.result.contains("nextCursor"));

  mcp::jsonrpc::Request templatesSecondPage;
  templatesSecondPage.id = kResourceTemplatesListPaginationRequestId + 1;
  templatesSecondPage.method = "resources/templates/list";
  templatesSecondPage.params = mcp::jsonrpc::JsonValue::object();
  (*templatesSecondPage.params)["cursor"] = firstTemplatePage.result["nextCursor"];

  const mcp::jsonrpc::Response secondTemplateResponse = dispatchRequest(*server, templatesSecondPage);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(secondTemplateResponse));
  const auto &secondTemplatePage = std::get<mcp::jsonrpc::SuccessResponse>(secondTemplateResponse);
  REQUIRE(secondTemplatePage.result["resourceTemplates"].size() == 5);
  REQUIRE_FALSE(secondTemplatePage.result.contains("nextCursor"));
}

TEST_CASE("Server resources/read missing URI returns resource not found", "[server][resources][errors]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, mcp::lifecycle::session::ResourcesCapability {}, std::nullopt, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));
  completeInitialization(*server);

  mcp::jsonrpc::Request request;
  request.id = kResourcesMissingReadRequestId;
  request.method = "resources/read";
  request.params = mcp::jsonrpc::JsonValue::object();
  (*request.params)["uri"] = "resource://missing";

  const mcp::jsonrpc::Response response = dispatchRequest(*server, request);
  assertErrorCode(response, mcp::JsonRpcErrorCode::kResourceNotFound);

  const auto &errorResponse = std::get<mcp::jsonrpc::ErrorResponse>(response);
  REQUIRE(errorResponse.error.code == -32002);
  REQUIRE(errorResponse.error.data.has_value());
  if (errorResponse.error.data.has_value())
  {
    REQUIRE((*errorResponse.error.data)["uri"].as<std::string>() == "resource://missing");
  }
}

TEST_CASE("Server resource subscriptions are capability-gated and emit update notifications", "[server][resources][subscriptions]")
{
  SECTION("Subscription methods are unavailable when resources.subscribe is not declared")
  {
    mcp::lifecycle::session::ResourcesCapability resourcesCapability;
    resourcesCapability.subscribe = false;

    mcp::ServerConfiguration configuration;
    configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, resourcesCapability, std::nullopt, std::nullopt, std::nullopt);

    auto server = mcp::Server::create(std::move(configuration));
    completeInitialization(*server);

    mcp::jsonrpc::Request subscribeRequest;
    subscribeRequest.id = kResourcesSubscribeGatingRequestId;
    subscribeRequest.method = "resources/subscribe";
    subscribeRequest.params = mcp::jsonrpc::JsonValue::object();
    (*subscribeRequest.params)["uri"] = "resource://anything";

    const mcp::jsonrpc::Response subscribeResponse = dispatchRequest(*server, subscribeRequest);
    assertErrorCode(subscribeResponse, mcp::JsonRpcErrorCode::kMethodNotFound);
  }

  SECTION("When enabled, subscribe and unsubscribe control resources/updated delivery")
  {
    mcp::lifecycle::session::ResourcesCapability resourcesCapability;
    resourcesCapability.subscribe = true;
    resourcesCapability.listChanged = true;

    mcp::ServerConfiguration configuration;
    configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, std::nullopt, resourcesCapability, std::nullopt, std::nullopt, std::nullopt);

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

    server->registerResource(makeResourceDefinition("resource://subscribed", "subscribed"),
                             [](const mcp::ResourceReadContext &) -> std::vector<mcp::ResourceContent>
                             {
                               return {
                                 mcp::ResourceContent::text("resource://subscribed", "value"),
                               };
                             });

    mcp::jsonrpc::RequestContext subscriberContext;
    subscriberContext.sessionId = "session-a";

    mcp::jsonrpc::Request subscribeRequest;
    subscribeRequest.id = kResourcesSubscribeRequestId;
    subscribeRequest.method = "resources/subscribe";
    subscribeRequest.params = mcp::jsonrpc::JsonValue::object();
    (*subscribeRequest.params)["uri"] = "resource://subscribed";

    const mcp::jsonrpc::Response subscribeResponse = dispatchRequest(*server, subscribeRequest, subscriberContext);
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(subscribeResponse));

    REQUIRE(server->notifyResourceUpdated("resource://subscribed", subscriberContext));

    mcp::jsonrpc::RequestContext otherContext;
    otherContext.sessionId = "session-b";
    REQUIRE_FALSE(server->notifyResourceUpdated("resource://subscribed", otherContext));

    mcp::jsonrpc::Request unsubscribeRequest;
    unsubscribeRequest.id = kResourcesUnsubscribeRequestId;
    unsubscribeRequest.method = "resources/unsubscribe";
    unsubscribeRequest.params = mcp::jsonrpc::JsonValue::object();
    (*unsubscribeRequest.params)["uri"] = "resource://subscribed";

    const mcp::jsonrpc::Response unsubscribeResponse = dispatchRequest(*server, unsubscribeRequest, subscriberContext);
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(unsubscribeResponse));

    REQUIRE_FALSE(server->notifyResourceUpdated("resource://subscribed", subscriberContext));

    std::size_t updatedNotifications = 0;
    std::size_t listChangedNotifications = 0;
    {
      const std::scoped_lock lock(messagesMutex);
      for (const auto &message : outboundMessages)
      {
        if (!std::holds_alternative<mcp::jsonrpc::Notification>(message))
        {
          continue;
        }

        const auto &notification = std::get<mcp::jsonrpc::Notification>(message);
        if (notification.method == "notifications/resources/updated")
        {
          ++updatedNotifications;
        }
        if (notification.method == "notifications/resources/list_changed")
        {
          ++listChangedNotifications;
        }
      }
    }

    REQUIRE(updatedNotifications == 1);
    REQUIRE(listChangedNotifications == 1);
  }
}

TEST_CASE("Server prompts list supports cursor pagination", "[server][prompts][list]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, mcp::lifecycle::session::PromptsCapability {}, std::nullopt, std::nullopt, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));
  for (std::size_t index = 0; index < kPromptsListPageSize + 5; ++index)
  {
    server->registerPrompt(makePromptDefinition("prompt-" + std::to_string(index)),
                           [](const mcp::PromptGetContext &) -> mcp::PromptGetResult
                           {
                             mcp::PromptGetResult result;
                             mcp::PromptMessage message;
                             message.role = "user";
                             message.content = mcp::jsonrpc::JsonValue::object();
                             message.content["type"] = "text";
                             message.content["text"] = "hello";
                             result.messages.push_back(std::move(message));
                             return result;
                           });
  }

  completeInitialization(*server);

  mcp::jsonrpc::Request firstPageRequest;
  firstPageRequest.id = kPromptsListPaginationRequestId;
  firstPageRequest.method = "prompts/list";
  firstPageRequest.params = mcp::jsonrpc::JsonValue::object();

  const mcp::jsonrpc::Response firstPageResponse = dispatchRequest(*server, firstPageRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(firstPageResponse));
  const auto &firstPage = std::get<mcp::jsonrpc::SuccessResponse>(firstPageResponse);
  REQUIRE(firstPage.result["prompts"].size() == kPromptsListPageSize);
  REQUIRE(firstPage.result.contains("nextCursor"));

  mcp::jsonrpc::Request secondPageRequest;
  secondPageRequest.id = kPromptsListPaginationRequestId + 1;
  secondPageRequest.method = "prompts/list";
  secondPageRequest.params = mcp::jsonrpc::JsonValue::object();
  (*secondPageRequest.params)["cursor"] = firstPage.result["nextCursor"];

  const mcp::jsonrpc::Response secondPageResponse = dispatchRequest(*server, secondPageRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(secondPageResponse));
  const auto &secondPage = std::get<mcp::jsonrpc::SuccessResponse>(secondPageResponse);
  REQUIRE(secondPage.result["prompts"].size() == 5);
  REQUIRE_FALSE(secondPage.result.contains("nextCursor"));

  mcp::jsonrpc::Request invalidCursorRequest;
  invalidCursorRequest.id = kPromptsListPaginationRequestId + 2;
  invalidCursorRequest.method = "prompts/list";
  invalidCursorRequest.params = mcp::jsonrpc::JsonValue::object();
  (*invalidCursorRequest.params)["cursor"] = "not-a-valid-cursor";

  const mcp::jsonrpc::Response invalidCursorResponse = dispatchRequest(*server, invalidCursorRequest);
  assertErrorCode(invalidCursorResponse, mcp::JsonRpcErrorCode::kInvalidParams);
}

TEST_CASE("Server list endpoint cursors are opaque, stable, and endpoint-scoped", "[server][utilities][pagination][list]")
{
  mcp::lifecycle::session::TasksCapability tasksCapability;
  tasksCapability.list = true;
  tasksCapability.toolsCall = true;

  mcp::ServerConfiguration configuration;
  configuration.capabilities =
    mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, mcp::lifecycle::session::PromptsCapability {}, mcp::lifecycle::session::ResourcesCapability {}, mcp::lifecycle::session::ToolsCapability {}, tasksCapability, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));

  for (std::size_t index = 0; index < kToolsListPageSize + 3; ++index)
  {
    server->registerTool(makeToolDefinition("tool-page-" + std::to_string(index)),
                         [](const mcp::ToolCallContext &) -> mcp::CallToolResult
                         {
                           mcp::CallToolResult result;
                           result.content = mcp::jsonrpc::JsonValue::array();
                           return result;
                         });
  }

  for (std::size_t index = 0; index < kResourcesListPageSize + 3; ++index)
  {
    const std::string uri = "resource://page-" + std::to_string(index);
    server->registerResource(makeResourceDefinition(uri, "resource-page-" + std::to_string(index)),
                             [uri](const mcp::ResourceReadContext &) -> std::vector<mcp::ResourceContent>
                             {
                               return {
                                 mcp::ResourceContent::text(uri, "payload", std::string("text/plain")),
                               };
                             });
  }

  for (std::size_t index = 0; index < kPromptsListPageSize + 3; ++index)
  {
    server->registerPrompt(makePromptDefinition("prompt-page-" + std::to_string(index)),
                           [](const mcp::PromptGetContext &) -> mcp::PromptGetResult
                           {
                             mcp::PromptGetResult result;
                             mcp::PromptMessage message;
                             message.role = "assistant";
                             message.content = mcp::jsonrpc::JsonValue::object();
                             message.content["type"] = "text";
                             message.content["text"] = "ok";
                             result.messages.push_back(std::move(message));
                             return result;
                           });
  }

  mcp::ToolDefinition taskTool = makeToolDefinition("task-page-tool");
  taskTool.execution = mcp::jsonrpc::JsonValue::object();
  (*taskTool.execution)["taskSupport"] = "optional";
  server->registerTool(taskTool,
                       [](const mcp::ToolCallContext &) -> mcp::CallToolResult
                       {
                         mcp::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
                         content["type"] = "text";
                         content["text"] = "task";
                         result.content.push_back(std::move(content));
                         return result;
                       });

  completeInitialization(*server);

  mcp::jsonrpc::RequestContext taskContext;
  taskContext.authContext = "auth-pagination";
  for (std::size_t index = 0; index < kTasksListPageSize + 3; ++index)
  {
    mcp::jsonrpc::Request taskCall;
    taskCall.id = static_cast<std::int64_t>(4000 + index);
    taskCall.method = "tools/call";
    taskCall.params = mcp::jsonrpc::JsonValue::object();
    (*taskCall.params)["name"] = "task-page-tool";
    (*taskCall.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
    (*taskCall.params)["arguments"]["value"] = "v";
    (*taskCall.params)["task"] = mcp::jsonrpc::JsonValue::object();

    const mcp::jsonrpc::Response taskCallResponse = dispatchRequest(*server, taskCall, taskContext);
    REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(taskCallResponse));
  }

  mcp::jsonrpc::Request toolsList;
  toolsList.id = std::int64_t {5001};
  toolsList.method = "tools/list";
  toolsList.params = mcp::jsonrpc::JsonValue::object();
  const mcp::jsonrpc::Response toolsListResponse = dispatchRequest(*server, toolsList);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(toolsListResponse));
  const auto &toolsFirstPage = std::get<mcp::jsonrpc::SuccessResponse>(toolsListResponse);
  REQUIRE(toolsFirstPage.result["tools"].size() == kToolsListPageSize);
  REQUIRE(toolsFirstPage.result.contains("nextCursor"));
  const std::string toolsCursor = toolsFirstPage.result["nextCursor"].as<std::string>();
  REQUIRE(toolsCursor != std::to_string(kToolsListPageSize));

  toolsList.id = std::int64_t {5006};
  const mcp::jsonrpc::Response toolsListStableResponse = dispatchRequest(*server, toolsList);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(toolsListStableResponse));
  const auto &toolsStablePage = std::get<mcp::jsonrpc::SuccessResponse>(toolsListStableResponse);
  REQUIRE(toolsStablePage.result["nextCursor"].as<std::string>() == toolsCursor);

  mcp::jsonrpc::Request resourcesList;
  resourcesList.id = std::int64_t {5002};
  resourcesList.method = "resources/list";
  resourcesList.params = mcp::jsonrpc::JsonValue::object();
  const mcp::jsonrpc::Response resourcesListResponse = dispatchRequest(*server, resourcesList);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(resourcesListResponse));
  const auto &resourcesFirstPage = std::get<mcp::jsonrpc::SuccessResponse>(resourcesListResponse);
  REQUIRE(resourcesFirstPage.result["resources"].size() == kResourcesListPageSize);
  REQUIRE(resourcesFirstPage.result.contains("nextCursor"));
  const std::string resourcesCursor = resourcesFirstPage.result["nextCursor"].as<std::string>();
  REQUIRE(resourcesCursor != std::to_string(kResourcesListPageSize));

  resourcesList.id = std::int64_t {5007};
  const mcp::jsonrpc::Response resourcesListStableResponse = dispatchRequest(*server, resourcesList);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(resourcesListStableResponse));
  const auto &resourcesStablePage = std::get<mcp::jsonrpc::SuccessResponse>(resourcesListStableResponse);
  REQUIRE(resourcesStablePage.result["nextCursor"].as<std::string>() == resourcesCursor);

  mcp::jsonrpc::Request promptsList;
  promptsList.id = std::int64_t {5003};
  promptsList.method = "prompts/list";
  promptsList.params = mcp::jsonrpc::JsonValue::object();
  const mcp::jsonrpc::Response promptsListResponse = dispatchRequest(*server, promptsList);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(promptsListResponse));
  const auto &promptsFirstPage = std::get<mcp::jsonrpc::SuccessResponse>(promptsListResponse);
  REQUIRE(promptsFirstPage.result["prompts"].size() == kPromptsListPageSize);
  REQUIRE(promptsFirstPage.result.contains("nextCursor"));
  const std::string promptsCursor = promptsFirstPage.result["nextCursor"].as<std::string>();
  REQUIRE(promptsCursor != std::to_string(kPromptsListPageSize));

  promptsList.id = std::int64_t {5008};
  const mcp::jsonrpc::Response promptsListStableResponse = dispatchRequest(*server, promptsList);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(promptsListStableResponse));
  const auto &promptsStablePage = std::get<mcp::jsonrpc::SuccessResponse>(promptsListStableResponse);
  REQUIRE(promptsStablePage.result["nextCursor"].as<std::string>() == promptsCursor);

  mcp::jsonrpc::Request tasksList;
  tasksList.id = std::int64_t {5004};
  tasksList.method = "tasks/list";
  tasksList.params = mcp::jsonrpc::JsonValue::object();
  const mcp::jsonrpc::Response tasksListResponse = dispatchRequest(*server, tasksList, taskContext);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(tasksListResponse));
  const auto &tasksFirstPage = std::get<mcp::jsonrpc::SuccessResponse>(tasksListResponse);
  REQUIRE(tasksFirstPage.result["tasks"].size() == kTasksListPageSize);
  REQUIRE(tasksFirstPage.result.contains("nextCursor"));
  const std::string tasksCursor = tasksFirstPage.result["nextCursor"].as<std::string>();
  REQUIRE(tasksCursor != std::to_string(kTasksListPageSize));

  tasksList.id = std::int64_t {5009};
  const mcp::jsonrpc::Response tasksListStableResponse = dispatchRequest(*server, tasksList, taskContext);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(tasksListStableResponse));
  const auto &tasksStablePage = std::get<mcp::jsonrpc::SuccessResponse>(tasksListStableResponse);
  REQUIRE(tasksStablePage.result["nextCursor"].as<std::string>() == tasksCursor);

  mcp::jsonrpc::Request tasksSecondPage;
  tasksSecondPage.id = std::int64_t {5005};
  tasksSecondPage.method = "tasks/list";
  tasksSecondPage.params = mcp::jsonrpc::JsonValue::object();
  (*tasksSecondPage.params)["cursor"] = tasksCursor;
  const mcp::jsonrpc::Response tasksSecondPageResponse = dispatchRequest(*server, tasksSecondPage, taskContext);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(tasksSecondPageResponse));
  const auto &tasksPageTwo = std::get<mcp::jsonrpc::SuccessResponse>(tasksSecondPageResponse);
  REQUIRE(tasksPageTwo.result["tasks"].size() == 3);
  REQUIRE_FALSE(tasksPageTwo.result.contains("nextCursor"));

  mcp::jsonrpc::Request toolsWithTaskCursor;
  toolsWithTaskCursor.id = std::int64_t {5101};
  toolsWithTaskCursor.method = "tools/list";
  toolsWithTaskCursor.params = mcp::jsonrpc::JsonValue::object();
  (*toolsWithTaskCursor.params)["cursor"] = tasksCursor;
  assertErrorCode(dispatchRequest(*server, toolsWithTaskCursor), mcp::JsonRpcErrorCode::kInvalidParams);

  mcp::jsonrpc::Request resourcesWithToolsCursor;
  resourcesWithToolsCursor.id = std::int64_t {5102};
  resourcesWithToolsCursor.method = "resources/list";
  resourcesWithToolsCursor.params = mcp::jsonrpc::JsonValue::object();
  (*resourcesWithToolsCursor.params)["cursor"] = toolsCursor;
  assertErrorCode(dispatchRequest(*server, resourcesWithToolsCursor), mcp::JsonRpcErrorCode::kInvalidParams);

  mcp::jsonrpc::Request promptsWithResourcesCursor;
  promptsWithResourcesCursor.id = std::int64_t {5103};
  promptsWithResourcesCursor.method = "prompts/list";
  promptsWithResourcesCursor.params = mcp::jsonrpc::JsonValue::object();
  (*promptsWithResourcesCursor.params)["cursor"] = resourcesCursor;
  assertErrorCode(dispatchRequest(*server, promptsWithResourcesCursor), mcp::JsonRpcErrorCode::kInvalidParams);

  mcp::jsonrpc::Request tasksWithPromptsCursor;
  tasksWithPromptsCursor.id = std::int64_t {5104};
  tasksWithPromptsCursor.method = "tasks/list";
  tasksWithPromptsCursor.params = mcp::jsonrpc::JsonValue::object();
  (*tasksWithPromptsCursor.params)["cursor"] = promptsCursor;
  assertErrorCode(dispatchRequest(*server, tasksWithPromptsCursor, taskContext), mcp::JsonRpcErrorCode::kInvalidParams);
}

TEST_CASE("Server prompts/get validates required and unknown arguments", "[server][prompts][get]")
{
  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, mcp::lifecycle::session::PromptsCapability {}, std::nullopt, std::nullopt, std::nullopt, std::nullopt);

  auto server = mcp::Server::create(std::move(configuration));

  std::size_t invocationCount = 0;
  server->registerPrompt(makePromptDefinition("explain-topic"),
                         [&invocationCount](const mcp::PromptGetContext &context) -> mcp::PromptGetResult
                         {
                           ++invocationCount;

                           mcp::PromptGetResult result;
                           result.description = "Explain a topic";

                           mcp::PromptMessage message;
                           message.role = "user";
                           message.content = mcp::jsonrpc::JsonValue::object();
                           message.content["type"] = "text";
                           message.content["text"] = "Explain: " + context.arguments["topic"].as<std::string>();
                           result.messages.push_back(std::move(message));
                           return result;
                         });

  completeInitialization(*server);

  mcp::jsonrpc::Request missingArgumentRequest;
  missingArgumentRequest.id = kPromptsGetMissingArgumentRequestId;
  missingArgumentRequest.method = "prompts/get";
  missingArgumentRequest.params = mcp::jsonrpc::JsonValue::object();
  (*missingArgumentRequest.params)["name"] = "explain-topic";

  const mcp::jsonrpc::Response missingArgumentResponse = dispatchRequest(*server, missingArgumentRequest);
  assertErrorCode(missingArgumentResponse, mcp::JsonRpcErrorCode::kInvalidParams);
  REQUIRE(invocationCount == 0);

  mcp::jsonrpc::Request unknownArgumentRequest;
  unknownArgumentRequest.id = kPromptsGetUnknownArgumentRequestId;
  unknownArgumentRequest.method = "prompts/get";
  unknownArgumentRequest.params = mcp::jsonrpc::JsonValue::object();
  (*unknownArgumentRequest.params)["name"] = "explain-topic";
  (*unknownArgumentRequest.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*unknownArgumentRequest.params)["arguments"]["topic"] = "mcp";
  (*unknownArgumentRequest.params)["arguments"]["extra"] = "unexpected";

  const mcp::jsonrpc::Response unknownArgumentResponse = dispatchRequest(*server, unknownArgumentRequest);
  assertErrorCode(unknownArgumentResponse, mcp::JsonRpcErrorCode::kInvalidParams);
  REQUIRE(invocationCount == 0);

  mcp::jsonrpc::Request invalidArgumentTypeRequest;
  invalidArgumentTypeRequest.id = kPromptsGetInvalidArgumentTypeRequestId;
  invalidArgumentTypeRequest.method = "prompts/get";
  invalidArgumentTypeRequest.params = mcp::jsonrpc::JsonValue::object();
  (*invalidArgumentTypeRequest.params)["name"] = "explain-topic";
  (*invalidArgumentTypeRequest.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*invalidArgumentTypeRequest.params)["arguments"]["topic"] = 42;

  const mcp::jsonrpc::Response invalidArgumentTypeResponse = dispatchRequest(*server, invalidArgumentTypeRequest);
  assertErrorCode(invalidArgumentTypeResponse, mcp::JsonRpcErrorCode::kInvalidParams);
  REQUIRE(invocationCount == 0);

  mcp::jsonrpc::Request validRequest;
  validRequest.id = kPromptsGetRequestId;
  validRequest.method = "prompts/get";
  validRequest.params = mcp::jsonrpc::JsonValue::object();
  (*validRequest.params)["name"] = "explain-topic";
  (*validRequest.params)["arguments"] = mcp::jsonrpc::JsonValue::object();
  (*validRequest.params)["arguments"]["topic"] = "pagination";

  const mcp::jsonrpc::Response validResponse = dispatchRequest(*server, validRequest);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(validResponse));
  const auto &success = std::get<mcp::jsonrpc::SuccessResponse>(validResponse);
  REQUIRE(success.result["description"].as<std::string>() == "Explain a topic");
  REQUIRE(success.result["messages"].size() == 1);
  REQUIRE(success.result["messages"][0]["role"].as<std::string>() == "user");
  REQUIRE(success.result["messages"][0]["content"]["type"].as<std::string>() == "text");
  REQUIRE(success.result["messages"][0]["content"]["text"].as<std::string>() == "Explain: pagination");
  REQUIRE(invocationCount == 1);
}

TEST_CASE("Server emits prompts list_changed notifications when enabled", "[server][prompts][notifications]")
{
  mcp::lifecycle::session::PromptsCapability promptsCapability;
  promptsCapability.listChanged = true;

  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, std::nullopt, std::nullopt, std::nullopt, std::nullopt);

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

  server->registerPrompt(makePromptDefinition("notify-prompt"),
                         [](const mcp::PromptGetContext &) -> mcp::PromptGetResult
                         {
                           mcp::PromptGetResult result;
                           mcp::PromptMessage message;
                           message.role = "assistant";
                           message.content = mcp::jsonrpc::JsonValue::object();
                           message.content["type"] = "text";
                           message.content["text"] = "ok";
                           result.messages.push_back(std::move(message));
                           return result;
                         });
  REQUIRE(server->unregisterPrompt("notify-prompt"));

  std::size_t listChangedNotifications = 0;
  {
    const std::scoped_lock lock(messagesMutex);
    for (const auto &message : outboundMessages)
    {
      if (!std::holds_alternative<mcp::jsonrpc::Notification>(message))
      {
        continue;
      }

      const auto &notification = std::get<mcp::jsonrpc::Notification>(message);
      if (notification.method == "notifications/prompts/list_changed")
      {
        ++listChangedNotifications;
      }
    }
  }

  REQUIRE(listChangedNotifications == 2);
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
