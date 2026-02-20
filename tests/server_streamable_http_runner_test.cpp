#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <mcp/http/sse.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/streamable_http_runner.hpp>
#include <mcp/server/tools.hpp>
#include <mcp/transport/http.hpp>
#include <mcp/version.hpp>

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
static auto createMinimalServer() -> std::shared_ptr<mcp::Server>
{
  auto server = mcp::Server::create();

  // Register at least one tool so the server has tools capability
  mcp::ToolDefinition toolDef;
  toolDef.name = "ping";
  toolDef.description = "A simple ping tool for testing";
  toolDef.inputSchema = mcp::jsonrpc::JsonValue::object();
  toolDef.inputSchema["type"] = "object";
  toolDef.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();

  server->registerTool(std::move(toolDef),
                       [](const mcp::ToolCallContext &) -> mcp::CallToolResult
                       {
                         mcp::CallToolResult result;
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
  mcp::StreamableHttpServerRunner runner(createMinimalServer);
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
  std::vector<std::shared_ptr<mcp::Server>> createdServers;

  auto countingFactory = [&factoryCount, &createdServers]() -> std::shared_ptr<mcp::Server>
  {
    auto server = createMinimalServer();
    factoryCount++;
    createdServers.push_back(server);
    return server;
  };

  // Create runner with requireSessionId=true
  mcp::StreamableHttpServerRunnerOptions options;
  options.transportOptions.http.endpoint.bindAddress = "127.0.0.1";
  options.transportOptions.http.endpoint.path = "/mcp";
  options.transportOptions.http.requireSessionId = true;

  mcp::StreamableHttpServerRunner runner(std::move(countingFactory), std::move(options));
  runner.start();

  const std::string baseUrl = "http://127.0.0.1:" + std::to_string(runner.localPort()) + "/mcp";

  // Create client runtime
  mcp_transport::HttpClientOptions clientOptions;
  clientOptions.endpointUrl = baseUrl;
  mcp_transport::HttpClientRuntime client(std::move(clientOptions));

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
  mcp::StreamableHttpServerRunnerOptions options;
  options.transportOptions.http.endpoint.bindAddress = "127.0.0.1";
  options.transportOptions.http.endpoint.path = "/mcp";
  options.transportOptions.http.requireSessionId = true;

  mcp::StreamableHttpServerRunner runner(createMinimalServer, std::move(options));
  runner.start();

  const std::string baseUrl = "http://127.0.0.1:" + std::to_string(runner.localPort()) + "/mcp";

  // Create client runtime
  mcp_transport::HttpClientOptions clientOptions;
  clientOptions.endpointUrl = baseUrl;
  mcp_transport::HttpClientRuntime client(std::move(clientOptions));

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
  mcp::StreamableHttpServerRunnerOptions options;
  options.transportOptions.http.endpoint.bindAddress = "127.0.0.1";
  options.transportOptions.http.endpoint.path = "/mcp";
  options.transportOptions.http.requireSessionId = true;

  mcp::StreamableHttpServerRunner runner(createMinimalServer, std::move(options));
  runner.start();

  const std::string baseUrl = "http://127.0.0.1:" + std::to_string(runner.localPort()) + "/mcp";

  // Create client runtime
  mcp_transport::HttpClientOptions clientOptions;
  clientOptions.endpointUrl = baseUrl;
  mcp_transport::HttpClientRuntime client(std::move(clientOptions));

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

TEST_CASE("StreamableHttpServerRunner initializes on first request when requireSessionId=false", "[server][streamable_http_runner]")
{
  // Track factory invocations
  std::atomic<int> factoryCount {0};

  auto countingFactory = [&factoryCount]() -> std::shared_ptr<mcp::Server>
  {
    factoryCount++;
    return createMinimalServer();
  };

  // Create runner with requireSessionId=false (single-server mode)
  mcp::StreamableHttpServerRunnerOptions options;
  options.transportOptions.http.endpoint.bindAddress = "127.0.0.1";
  options.transportOptions.http.endpoint.path = "/mcp";
  options.transportOptions.http.requireSessionId = false;

  mcp::StreamableHttpServerRunner runner(std::move(countingFactory), std::move(options));
  runner.start();

  const std::string baseUrl = "http://127.0.0.1:" + std::to_string(runner.localPort()) + "/mcp";

  // Create client runtime
  mcp_transport::HttpClientOptions clientOptions;
  clientOptions.endpointUrl = baseUrl;
  mcp_transport::HttpClientRuntime client(std::move(clientOptions));

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
