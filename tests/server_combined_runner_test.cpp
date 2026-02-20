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
#include <mcp/transport/http.hpp>
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

// Helper to verify a port is released by attempting to bind an HTTP server to it
// Uses retries with exponential backoff to handle TIME_WAIT socket state
static auto verifyPortReleased(std::uint16_t port, std::chrono::milliseconds timeout) -> std::unique_ptr<mcp::transport::HttpServerRuntime>
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  auto sleepDuration = std::chrono::milliseconds {50};

  while (std::chrono::steady_clock::now() < deadline)
  {
    try
    {
      mcp::transport::HttpServerOptions options;
      options.endpoint.bindAddress = "127.0.0.1";
      options.endpoint.port = port;

      auto server = std::make_unique<mcp::transport::HttpServerRuntime>(std::move(options));
      server->start();

      if (server->isRunning())
      {
        return server;
      }
    }
    catch (...)
    {
      // Port still in use, retry with backoff
    }

    std::this_thread::sleep_for(sleepDuration);
    sleepDuration = std::min(sleepDuration * 2, std::chrono::milliseconds {500});
  }

  // Final attempt - return nullptr if it fails (port still in use)
  try
  {
    mcp::transport::HttpServerOptions options;
    options.endpoint.bindAddress = "127.0.0.1";
    options.endpoint.port = port;

    auto server = std::make_unique<mcp::transport::HttpServerRuntime>(std::move(options));
    server->start();

    if (server->isRunning())
    {
      return server;
    }
  }
  catch (...)
  {
    // Port still in use
  }

  return nullptr;
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

  // Create runner as shared_ptr to safely share ownership with stdio thread
  auto runner = std::make_shared<mcp::CombinedServerRunner>(createMinimalServer, options);

  // Start HTTP server first (non-blocking)
  runner->startHttp();

  // Verify HTTP is running
  REQUIRE(runner->isHttpRunning());
  const std::uint16_t httpPort = runner->localPort();
  REQUIRE(httpPort > 0);

  // Run stdio in a separate thread to avoid blocking
  std::atomic<bool> stdioCompleted {false};
  std::thread stdioThread(
    [runner, &stdioCompleted]()
    {
      runner->runStdio();
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
      // Force stop - runner stays alive because shared_ptr keeps it alive
      // for the detached thread. The thread will complete safely.
      runner->stop();

      // Give the detached thread a moment to complete its cleanup
      // without blocking indefinitely
      std::this_thread::sleep_for(std::chrono::milliseconds {50});

      stdioThread.detach();
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
  runner->stopHttp();
  REQUIRE_FALSE(runner->isHttpRunning());
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

TEST_CASE("CombinedServerRunner destructor releases HTTP port without explicit stop", "[server][combined_runner]")
{
  // Create options with HTTP-only mode
  mcp::CombinedServerRunnerOptions options;
  options.enableHttp = true;
  options.httpOptions.transportOptions.http.endpoint.bindAddress = "127.0.0.1";

  std::uint16_t capturedPort = 0;

  // Create and start runner in a scope, then destroy without stopping
  {
    mcp::CombinedServerRunner runner(createMinimalServer, options);
    runner.startHttp();

    REQUIRE(runner.isHttpRunning());
    capturedPort = runner.localPort();
    REQUIRE(capturedPort > 0);

    // Destroy runner without calling stopHttp() or stop()
    // The destructor should clean up the HTTP listener and release the port
  }

  // Verify the port is released by successfully binding a new HttpServerRuntime
  // Uses bounded retry logic to handle TIME_WAIT socket state
  auto testServer = verifyPortReleased(capturedPort, std::chrono::seconds {30});

  // Port release must be verified within bounded time - fail if not released
  REQUIRE(testServer != nullptr);
  REQUIRE(testServer->isRunning());
  REQUIRE(testServer->localPort() == capturedPort);
  testServer->stop();
  REQUIRE_FALSE(testServer->isRunning());
}

TEST_CASE("CombinedServerRunner move assignment cleans up overwritten runner's HTTP port", "[server][combined_runner]")
{
  // Test strategy: Use a specific port for runnerB to verify cleanup
  // RunnerA uses ephemeral port

  // Create runnerA first (ephemeral port)
  mcp::CombinedServerRunnerOptions optionsA;
  optionsA.enableHttp = true;
  optionsA.httpOptions.transportOptions.http.endpoint.bindAddress = "127.0.0.1";

  mcp::CombinedServerRunner runnerA(createMinimalServer, optionsA);
  runnerA.startHttp();

  REQUIRE(runnerA.isHttpRunning());
  const std::uint16_t portA = runnerA.localPort();
  REQUIRE(portA > 0);

  // Create runnerB with a specific high port that we control
  mcp::CombinedServerRunnerOptions optionsB;
  optionsB.enableHttp = true;
  optionsB.httpOptions.transportOptions.http.endpoint.bindAddress = "127.0.0.1";
  optionsB.httpOptions.transportOptions.http.endpoint.port = 0;  // Let OS assign

  mcp::CombinedServerRunner runnerB(createMinimalServer, optionsB);
  runnerB.startHttp();

  REQUIRE(runnerB.isHttpRunning());
  const std::uint16_t portB = runnerB.localPort();
  REQUIRE(portB > 0);

  // Skip if ports are the same - can't verify cleanup in that case
  if (portB == portA)
  {
    runnerB.stopHttp();
    runnerA.stopHttp();
    SKIP("Both runners got same ephemeral port - cannot verify cleanup");
  }

  // Move-assign runnerA to runnerB
  // This should stop runnerB's old HTTP and release portB
  runnerB = std::move(runnerA);

  // runnerB now owns runnerA's HTTP
  REQUIRE(runnerB.isHttpRunning());
  REQUIRE(runnerB.localPort() == portA);

  // Explicitly destroy runnerA to trigger its destructor (cleanup of moved-from state)
  runnerA = mcp::CombinedServerRunner(createMinimalServer, optionsA);

  // Verify portB was released - try to bind to it
  // Uses bounded retry logic for TIME_WAIT handling
  auto testServer = verifyPortReleased(portB, std::chrono::seconds {30});

  // Port release must be verified within bounded time - fail if not released
  REQUIRE(testServer != nullptr);
  REQUIRE(testServer->isRunning());
  REQUIRE(testServer->localPort() == portB);
  testServer->stop();
  REQUIRE_FALSE(testServer->isRunning());

  // Clean up runnerB
  runnerB.stopHttp();
  REQUIRE_FALSE(runnerB.isHttpRunning());
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
