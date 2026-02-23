#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/http/all.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/sdk/version.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/streamable_http_runner.hpp>
#include <mcp/server/all.hpp>
#include <mcp/transport/all.hpp>

namespace
{

namespace mcp_transport = mcp::transport;
namespace mcp_http = mcp::transport::http;

// Helper to create an initialize request JSON with the latest protocol version
static auto makeInitializeRequestJson() -> std::string
{
  return R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":")" + std::string(mcp::kLatestProtocolVersion)
    + R"(","capabilities":{},"clientInfo":{"name":"test-client","version":"1.0.0"}}})";
}

// Helper to create a notifications/initialized notification JSON
static auto makeInitializedNotificationJson() -> std::string
{
  return R"({"jsonrpc":"2.0","method":"notifications/initialized"})";
}

// Helper to create a server notification JSON
static auto makeServerNotificationJson() -> std::string
{
  return R"({"jsonrpc":"2.0","method":"test/notification","params":{}})";
}

// Helper to make HTTP request with optional session ID
static auto makeRequest(mcp_http::ServerRequestMethod method,
                        std::string path,
                        std::optional<std::string_view> body = std::nullopt,
                        std::optional<std::string_view> sessionId = std::nullopt,
                        std::optional<std::string_view> accept = std::nullopt,
                        std::optional<std::string_view> lastEventId = std::nullopt,
                        std::optional<std::string_view> protocolVersion = std::nullopt) -> mcp_http::ServerRequest
{
  mcp_http::ServerRequest request;
  request.method = method;
  request.path = std::move(path);

  if (body.has_value())
  {
    request.body = std::string(*body);
    mcp_http::setHeader(request.headers, mcp_http::kHeaderContentType, "application/json");
  }

  if (sessionId.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderMcpSessionId, std::string(*sessionId));
  }

  if (accept.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderAccept, std::string(*accept));
  }

  if (lastEventId.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderLastEventId, std::string(*lastEventId));
  }

  if (protocolVersion.has_value())
  {
    mcp_http::setHeader(request.headers, mcp_http::kHeaderMcpProtocolVersion, std::string(*protocolVersion));
  }

  return request;
}

// Minimal ServerFactory that creates a Server with at least one tool registered
// This is required for initialize to succeed (server needs to have capabilities)
static auto createMinimalServer() -> std::shared_ptr<mcp::server::Server>
{
  auto server = mcp::server::Server::create();

  // Register at least one tool so the server has tools capability
  mcp::server::ToolDefinition toolDef;
  toolDef.name = "ping";
  toolDef.description = "A simple ping tool for testing";
  toolDef.inputSchema = mcp::jsonrpc::JsonValue::object();
  toolDef.inputSchema["type"] = "object";
  toolDef.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();

  server->registerTool(std::move(toolDef),
                       [](const mcp::server::ToolCallContext &) -> mcp::server::CallToolResult
                       {
                         mcp::server::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         result.content.push_back(mcp::jsonrpc::JsonValue::object());
                         result.content[0]["type"] = "text";
                         result.content[0]["text"] = "pong";
                         return result;
                       });

  return server;
}

}  // namespace

TEST_CASE("StreamableHttpServerRunner starts and stops cleanly on ephemeral port", "[server][streamable_http_runner]")
{
  // Create runner with factory that produces minimal server
  mcp::server::StreamableHttpServerRunner runner(createMinimalServer);
  runner.start();

  // Verify server is running and has a valid port
  REQUIRE(runner.isRunning());
  REQUIRE(runner.localPort() > 0);

  // Stop the runner
  runner.stop();

  // Verify server is stopped
  REQUIRE_FALSE(runner.isRunning());
}

TEST_CASE("StreamableHttpServerRunner creates independent sessions with requireSessionId=true", "[server][streamable_http_runner]")
{
  // Track factory invocations
  std::atomic<int> factoryCount {0};
  std::vector<std::shared_ptr<mcp::server::Server>> createdServers;

  auto countingFactory = [&factoryCount, &createdServers]() -> std::shared_ptr<mcp::server::Server>
  {
    auto server = createMinimalServer();
    factoryCount++;
    createdServers.push_back(server);
    return server;
  };

  // Create runner with requireSessionId=true
  mcp::server::StreamableHttpServerRunnerOptions options;
  options.transportOptions.http.endpoint.bindAddress = "127.0.0.1";
  options.transportOptions.http.endpoint.path = "/mcp";
  options.transportOptions.http.requireSessionId = true;

  mcp::server::StreamableHttpServerRunner runner(std::move(countingFactory), std::move(options));
  runner.start();

  const std::string baseUrl = "http://127.0.0.1:" + std::to_string(runner.localPort()) + "/mcp";

  // Create client runtime
  mcp_transport::http::HttpClientOptions clientOptions;
  clientOptions.endpointUrl = baseUrl;
  mcp_transport::http::HttpClientRuntime client(std::move(clientOptions));

  // Step 1: Initialize session A (no session ID in request, expect MCP-Session-Id in response)
  mcp_http::ServerResponse initResponseA = client.execute(makeRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeInitializeRequestJson()));

  REQUIRE(initResponseA.statusCode == 200);
  const std::optional<std::string> sessionIdA = mcp_http::getHeader(initResponseA.headers, mcp_http::kHeaderMcpSessionId);
  REQUIRE(sessionIdA.has_value());
  REQUIRE_FALSE(sessionIdA->empty());

  // Step 2: Send notifications/initialized for session A
  mcp_http::ServerResponse notificationA = client.execute(makeRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeInitializedNotificationJson(), *sessionIdA));

  REQUIRE(notificationA.statusCode == 202);

  // Step 3: Initialize session B (no session ID in request, expect different MCP-Session-Id)
  mcp_http::ServerResponse initResponseB = client.execute(makeRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeInitializeRequestJson()));

  REQUIRE(initResponseB.statusCode == 200);
  const std::optional<std::string> sessionIdB = mcp_http::getHeader(initResponseB.headers, mcp_http::kHeaderMcpSessionId);
  REQUIRE(sessionIdB.has_value());
  REQUIRE_FALSE(sessionIdB->empty());

  // Verify the two session IDs are different
  REQUIRE(*sessionIdA != *sessionIdB);

  // Step 4: Send notifications/initialized for session B
  mcp_http::ServerResponse notificationB = client.execute(makeRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeInitializedNotificationJson(), *sessionIdB));

  REQUIRE(notificationB.statusCode == 202);

  // Step 5: Assert factory count == 2 (one server per session)
  REQUIRE(factoryCount == 2);
  REQUIRE(createdServers.size() == 2);

  runner.stop();
}

TEST_CASE("StreamableHttpServerRunner supports SSE endpoint with valid session", "[server][streamable_http_runner]")
{
  // Create runner with requireSessionId=true
  mcp::server::StreamableHttpServerRunnerOptions options;
  options.transportOptions.http.endpoint.bindAddress = "127.0.0.1";
  options.transportOptions.http.endpoint.path = "/mcp";
  options.transportOptions.http.requireSessionId = true;

  mcp::server::StreamableHttpServerRunner runner(createMinimalServer, std::move(options));
  runner.start();

  const std::string baseUrl = "http://127.0.0.1:" + std::to_string(runner.localPort()) + "/mcp";

  // Create client runtime
  mcp_transport::http::HttpClientOptions clientOptions;
  clientOptions.endpointUrl = baseUrl;
  mcp_transport::http::HttpClientRuntime client(std::move(clientOptions));

  // Initialize session A
  mcp_http::ServerResponse initResponseA = client.execute(makeRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeInitializeRequestJson()));

  REQUIRE(initResponseA.statusCode == 200);
  const std::optional<std::string> sessionIdA = mcp_http::getHeader(initResponseA.headers, mcp_http::kHeaderMcpSessionId);
  REQUIRE(sessionIdA.has_value());

  // Send notifications/initialized for session A
  mcp_http::ServerResponse notificationA = client.execute(makeRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeInitializedNotificationJson(), *sessionIdA));

  REQUIRE(notificationA.statusCode == 202);

  // Open a GET SSE listen request for session A
  mcp_http::ServerResponse sseResponse = client.execute(makeRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, *sessionIdA, "text/event-stream"));

  // Verify SSE endpoint works when session is valid - check body content and status
  // Note: response.sse field may not be populated in all cases, but body will contain SSE format
  REQUIRE(sseResponse.statusCode == 200);
  // Verify content-type is text/event-stream
  const std::optional<std::string> contentType = mcp_http::getHeader(sseResponse.headers, mcp_http::kHeaderContentType);
  REQUIRE(contentType.has_value());
  REQUIRE(*contentType == "text/event-stream");
  // Verify body contains valid SSE (id field should be present)
  REQUIRE(sseResponse.body.find("id:") != std::string::npos);

  runner.stop();
}

TEST_CASE("StreamableHttpServerRunner rejects requests without session when requireSessionId=true", "[server][streamable_http_runner]")
{
  // Create runner with requireSessionId=true
  mcp::server::StreamableHttpServerRunnerOptions options;
  options.transportOptions.http.endpoint.bindAddress = "127.0.0.1";
  options.transportOptions.http.endpoint.path = "/mcp";
  options.transportOptions.http.requireSessionId = true;

  mcp::server::StreamableHttpServerRunner runner(createMinimalServer, std::move(options));
  runner.start();

  const std::string baseUrl = "http://127.0.0.1:" + std::to_string(runner.localPort()) + "/mcp";

  // Create client runtime
  mcp_transport::http::HttpClientOptions clientOptions;
  clientOptions.endpointUrl = baseUrl;
  mcp_transport::http::HttpClientRuntime client(std::move(clientOptions));

  // Try to send a request without a session ID (other than initialize)
  // First initialize to get a session
  mcp_http::ServerResponse initResponse = client.execute(makeRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeInitializeRequestJson()));

  REQUIRE(initResponse.statusCode == 200);
  const std::optional<std::string> sessionId = mcp_http::getHeader(initResponse.headers, mcp_http::kHeaderMcpSessionId);
  REQUIRE(sessionId.has_value());

  // Send notifications/initialized
  mcp_http::ServerResponse notificationResponse = client.execute(makeRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeInitializedNotificationJson(), *sessionId));

  REQUIRE(notificationResponse.statusCode == 202);

  // Now try to send a non-initialize request without session ID - expect HTTP 400
  mcp_http::ServerResponse missingSessionResponse = client.execute(makeRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeInitializedNotificationJson()));

  REQUIRE(missingSessionResponse.statusCode == 400);

  runner.stop();
}

TEST_CASE("StreamableHttpServerRunner routes outbound notifications via SSE", "[server][streamable_http_runner]")
{
  // Track factory invocations and capture created servers
  std::atomic<int> factoryCount {0};
  std::vector<std::shared_ptr<mcp::server::Server>> createdServers;

  auto countingFactory = [&factoryCount, &createdServers]() -> std::shared_ptr<mcp::server::Server>
  {
    auto server = createMinimalServer();
    factoryCount++;
    createdServers.push_back(server);
    return server;
  };

  // Create runner with requireSessionId=true
  mcp::server::StreamableHttpServerRunnerOptions options;
  options.transportOptions.http.endpoint.bindAddress = "127.0.0.1";
  options.transportOptions.http.endpoint.path = "/mcp";
  options.transportOptions.http.requireSessionId = true;

  mcp::server::StreamableHttpServerRunner runner(std::move(countingFactory), std::move(options));
  runner.start();

  const std::string baseUrl = "http://127.0.0.1:" + std::to_string(runner.localPort()) + "/mcp";

  // Create client runtime
  mcp_transport::http::HttpClientOptions clientOptions;
  clientOptions.endpointUrl = baseUrl;
  mcp_transport::http::HttpClientRuntime client(std::move(clientOptions));

  // Step 1: Initialize session A (POST initialize), capture sessionIdA
  mcp_http::ServerResponse initResponseA = client.execute(makeRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeInitializeRequestJson()));

  REQUIRE(initResponseA.statusCode == 200);
  const std::optional<std::string> sessionIdA = mcp_http::getHeader(initResponseA.headers, mcp_http::kHeaderMcpSessionId);
  REQUIRE(sessionIdA.has_value());
  REQUIRE_FALSE(sessionIdA->empty());

  // Send notifications/initialized to complete session initialization
  mcp_http::ServerResponse notificationInit = client.execute(makeRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeInitializedNotificationJson(), *sessionIdA));
  REQUIRE(notificationInit.statusCode == 202);

  // Verify factory was called once and server was created
  REQUIRE(factoryCount == 1);
  REQUIRE(createdServers.size() == 1);

  // Step 2 & 3: Open SSE stream for session A via GET with Accept, MCP-Session-Id, and MCP-Protocol-Version
  mcp_http::ServerResponse sseResponse = client.execute(
    makeRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, *sessionIdA, "text/event-stream", std::nullopt, std::string(mcp::kLatestProtocolVersion)));

  REQUIRE(sseResponse.statusCode == 200);

  // Extract Last-Event-ID from the first SSE response
  const std::optional<std::string> contentType = mcp_http::getHeader(sseResponse.headers, mcp_http::kHeaderContentType);
  REQUIRE(contentType.has_value());
  REQUIRE(*contentType == "text/event-stream");

  // Parse SSE events to get the event ID for polling
  std::string sseBody = sseResponse.body;
  REQUIRE(!sseBody.empty());

  auto events = mcp::http::sse::parseEvents(sseBody);
  REQUIRE(!events.empty());

  // Find the event with an ID (this is the cursor for polling)
  std::optional<std::string> lastEventId;
  for (const auto &event : events)
  {
    if (event.id.has_value() && !event.id->empty())
    {
      lastEventId = event.id;
      break;
    }
  }
  REQUIRE(lastEventId.has_value());

  // Step 4: Use the captured Server instance for session A
  REQUIRE(createdServers.size() == 1);
  auto &serverA = createdServers[0];

  // Step 5: Call sendNotification on the server
  mcp::jsonrpc::Notification testNotification;
  testNotification.method = "test/notification";
  testNotification.params = mcp::jsonrpc::JsonValue::object();
  (*testNotification.params)["message"] = "hello from server";

  mcp::jsonrpc::RequestContext context;
  context.sessionId = *sessionIdA;
  context.protocolVersion = std::string(mcp::kLatestProtocolVersion);

  serverA->sendNotification(context, testNotification);

  // Step 6: Poll the SSE stream using Last-Event-ID
  mcp_http::ServerResponse polledResponse = client.execute(
    makeRequest(mcp_http::ServerRequestMethod::kGet, "/mcp", std::nullopt, *sessionIdA, "text/event-stream", *lastEventId, std::string(mcp::kLatestProtocolVersion)));

  REQUIRE(polledResponse.statusCode == 200);

  // Step 7: Parse SSE events from polled response
  std::string polledBody = polledResponse.body;
  REQUIRE(!polledBody.empty());

  auto polledEvents = mcp::http::sse::parseEvents(polledBody);

  // Find the notification event
  bool foundNotification = false;
  for (const auto &event : polledEvents)
  {
    if (!event.data.empty())
    {
      // Parse event.data as JSON-RPC message
      auto message = mcp::jsonrpc::parseMessage(event.data);

      // Check if it's a notification with the expected method
      if (std::holds_alternative<mcp::jsonrpc::Notification>(message))
      {
        const auto &receivedNotification = std::get<mcp::jsonrpc::Notification>(message);
        if (receivedNotification.method == "test/notification")
        {
          foundNotification = true;
          // Verify the params contain our message
          if (receivedNotification.params.has_value())
          {
            REQUIRE(receivedNotification.params->contains("message"));
            REQUIRE((*receivedNotification.params)["message"].as<std::string>() == "hello from server");
          }
          break;
        }
      }
    }
  }

  REQUIRE(foundNotification);

  runner.stop();
}

TEST_CASE("StreamableHttpServerRunner initializes on first request when requireSessionId=false", "[server][streamable_http_runner]")
{
  // Track factory invocations
  std::atomic<int> factoryCount {0};

  auto countingFactory = [&factoryCount]() -> std::shared_ptr<mcp::server::Server>
  {
    factoryCount++;
    return createMinimalServer();
  };

  // Create runner with requireSessionId=false (single-server mode)
  mcp::server::StreamableHttpServerRunnerOptions options;
  options.transportOptions.http.endpoint.bindAddress = "127.0.0.1";
  options.transportOptions.http.endpoint.path = "/mcp";
  options.transportOptions.http.requireSessionId = false;

  mcp::server::StreamableHttpServerRunner runner(std::move(countingFactory), std::move(options));
  runner.start();

  const std::string baseUrl = "http://127.0.0.1:" + std::to_string(runner.localPort()) + "/mcp";

  // Create client runtime
  mcp_transport::http::HttpClientOptions clientOptions;
  clientOptions.endpointUrl = baseUrl;
  mcp_transport::http::HttpClientRuntime client(std::move(clientOptions));

  // Verify factory not yet called (server not created yet)
  REQUIRE(factoryCount == 0);

  // Send initialize request
  mcp_http::ServerResponse initResponse = client.execute(makeRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeInitializeRequestJson()));

  REQUIRE(initResponse.statusCode == 200);

  // Verify factory was called once (server created on first initialize)
  REQUIRE(factoryCount == 1);

  // Send notifications/initialized
  mcp_http::ServerResponse notificationResponse = client.execute(makeRequest(mcp_http::ServerRequestMethod::kPost, "/mcp", makeInitializedNotificationJson()));

  REQUIRE(notificationResponse.statusCode == 202);

  runner.stop();
}
