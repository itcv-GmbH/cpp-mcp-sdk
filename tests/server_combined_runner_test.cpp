#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/server/combined_runner.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/tools.hpp>
#include <mcp/version.hpp>

namespace
{

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

TEST_CASE("CombinedServerRunner handles stdio-only mode with in-memory streams", "[server][combined_runner]")
{
  // Create input: initialize request + notification + EOF
  std::ostringstream inputStream;
  inputStream << makeInitializeRequestJson() << '\n';
  inputStream << makeInitializedNotificationJson() << '\n';

  std::istringstream input(inputStream.str());
  std::ostringstream output;
  std::ostringstream stderr;

  // Create options with stdio-only mode and custom streams
  mcp::CombinedServerRunnerOptions options;
  options.enableStdio = true;
  options.stdioInput = &input;
  options.stdioOutput = &output;
  options.stdioError = &stderr;

  // Create runner with factory that produces minimal server
  mcp::CombinedServerRunner runner(createMinimalServer, options);

  // Run stdio (blocking)
  runner.runStdio();

  // Parse stdout - should contain a valid JSON-RPC response line
  const std::string outputStr = output.str();
  REQUIRE_FALSE(outputStr.empty());

  const auto message = mcp::jsonrpc::parseMessage(outputStr);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(message));

  const auto &response = std::get<mcp::jsonrpc::SuccessResponse>(message);
  REQUIRE(std::get<std::int64_t>(response.id) == std::int64_t {1});
  REQUIRE(response.result.is_object());
}

TEST_CASE("CombinedServerRunner handles HTTP-only mode on ephemeral port", "[server][combined_runner]")
{
  // Create options with HTTP-only mode
  mcp::CombinedServerRunnerOptions options;
  options.enableHttp = true;

  // Configure HTTP to use localhost and ephemeral port
  options.httpOptions.transportOptions.http.endpoint.bindAddress = "127.0.0.1";

  // Create runner with factory that produces minimal server
  mcp::CombinedServerRunner runner(createMinimalServer, options);

  // Start HTTP server (non-blocking)
  runner.startHttp();

  // Verify server is running and has a valid port
  REQUIRE(runner.isHttpRunning());
  REQUIRE(runner.localPort() > 0);
  REQUIRE(runner.localPort() <= std::uint16_t {65535});

  // Stop HTTP server
  runner.stopHttp();

  // Verify server is stopped
  REQUIRE_FALSE(runner.isHttpRunning());
}

TEST_CASE("CombinedServerRunner handles both-enabled mode without hangs", "[server][combined_runner]")
{
  // Create input: initialize request + notification + EOF
  std::ostringstream inputStream;
  inputStream << makeInitializeRequestJson() << '\n';
  inputStream << makeInitializedNotificationJson() << '\n';

  std::istringstream input(inputStream.str());
  std::ostringstream output;
  std::ostringstream stderr;

  // Create options with both HTTP and stdio enabled
  mcp::CombinedServerRunnerOptions options;
  options.enableStdio = true;
  options.enableHttp = true;
  options.httpOptions.transportOptions.http.endpoint.bindAddress = "127.0.0.1";
  options.stdioInput = &input;
  options.stdioOutput = &output;
  options.stdioError = &stderr;

  // Create runner with factory that produces minimal server
  mcp::CombinedServerRunner runner(createMinimalServer, options);

  // Start HTTP server first (non-blocking)
  runner.startHttp();

  // Verify HTTP is running
  REQUIRE(runner.isHttpRunning());
  const std::uint16_t httpPort = runner.localPort();
  REQUIRE(httpPort > 0);

  // Run stdio in a separate thread to avoid blocking
  std::atomic<bool> stdioCompleted {false};
  std::thread stdioThread(
    [&runner, &stdioCompleted]()
    {
      runner.runStdio();
      stdioCompleted = true;
    });

  // Wait for stdio to complete (it will complete when EOF is reached)
  // Use a reasonable timeout
  constexpr auto kTimeoutMs = std::chrono::milliseconds {5000};
  const auto startTime = std::chrono::steady_clock::now();

  while (!stdioCompleted)
  {
    const auto elapsed = std::chrono::steady_clock::now() - startTime;
    if (elapsed > kTimeoutMs)
    {
      // Force stop and fail
      runner.stopHttp();
      stdioThread.join();
      FAIL("Stdio thread did not complete within timeout");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds {10});
  }

  // Join the stdio thread
  stdioThread.join();

  // Verify stdio completed with valid output
  const std::string outputStr = output.str();
  REQUIRE_FALSE(outputStr.empty());

  const auto message = mcp::jsonrpc::parseMessage(outputStr);
  REQUIRE(std::holds_alternative<mcp::jsonrpc::SuccessResponse>(message));

  // Stop HTTP server cleanly
  runner.stopHttp();
  REQUIRE_FALSE(runner.isHttpRunning());
}

TEST_CASE("CombinedServerRunner move constructor allows destruction of moved-from object", "[server][combined_runner]")
{
  // Create options with HTTP-only mode
  mcp::CombinedServerRunnerOptions options;
  options.enableHttp = true;
  options.httpOptions.transportOptions.http.endpoint.bindAddress = "127.0.0.1";

  // Create first runner
  mcp::CombinedServerRunner runner1(createMinimalServer, options);
  runner1.startHttp();

  REQUIRE(runner1.isHttpRunning());
  const std::uint16_t port = runner1.localPort();
  REQUIRE(port > 0);

  // Move construct a new runner
  mcp::CombinedServerRunner runner2(std::move(runner1));

  // The moved-from runner should be in a valid state (but not running)
  // Note: After move, runner1 is in a valid but unspecified state
  // We can destroy it without crashing

  // The moved-to runner should be running and have the same port
  REQUIRE(runner2.isHttpRunning());
  REQUIRE(runner2.localPort() == port);

  // Stop the moved-to runner
  runner2.stopHttp();
  REQUIRE_FALSE(runner2.isHttpRunning());
}

TEST_CASE("CombinedServerRunner move assignment allows destruction of moved-from object", "[server][combined_runner]")
{
  // Create options with HTTP-only mode
  mcp::CombinedServerRunnerOptions options;
  options.enableHttp = true;
  options.httpOptions.transportOptions.http.endpoint.bindAddress = "127.0.0.1";

  // Create first runner
  mcp::CombinedServerRunner runner1(createMinimalServer, options);
  runner1.startHttp();

  REQUIRE(runner1.isHttpRunning());
  const std::uint16_t port1 = runner1.localPort();
  REQUIRE(port1 > 0);

  // Create second runner (will be overwritten)
  mcp::CombinedServerRunnerOptions options2;
  options2.enableHttp = true;
  mcp::CombinedServerRunner runner2(createMinimalServer, options2);
  runner2.startHttp();

  REQUIRE(runner2.isHttpRunning());
  const std::uint16_t port2 = runner2.localPort();
  REQUIRE(port2 > 0);

  // Move assign runner1 to runner2
  runner2 = std::move(runner1);

  // The moved-from runner (runner1) should be valid to destroy
  // The moved-to runner (runner2) should now be running with runner1's port
  REQUIRE(runner2.isHttpRunning());
  REQUIRE(runner2.localPort() == port1);

  // Stop the moved-to runner
  runner2.stopHttp();
  REQUIRE_FALSE(runner2.isHttpRunning());
}

TEST_CASE("CombinedServerRunner stop() stops HTTP transport", "[server][combined_runner]")
{
  // Create options with both HTTP and stdio enabled
  mcp::CombinedServerRunnerOptions options;
  options.enableHttp = true;
  options.httpOptions.transportOptions.http.endpoint.bindAddress = "127.0.0.1";

  mcp::CombinedServerRunner runner(createMinimalServer, options);

  // Start HTTP
  runner.startHttp();
  REQUIRE(runner.isHttpRunning());

  // Stop both (stop() stops HTTP)
  runner.stop();
  REQUIRE_FALSE(runner.isHttpRunning());
}
