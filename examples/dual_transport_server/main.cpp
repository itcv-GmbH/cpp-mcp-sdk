#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/combined_runner.hpp>
#include <mcp/server/prompts.hpp>
#include <mcp/server/resources.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/stdio_runner.hpp>
#include <mcp/server/streamable_http_runner.hpp>
#include <mcp/server/tools.hpp>
#include <unistd.h>

namespace
{

constexpr std::int64_t kDelayedEchoMilliseconds = 300;
constexpr const char *kHttpPath = "/mcp";
constexpr const char *kHttpBind = "127.0.0.1";
constexpr std::uint16_t kHttpPort = 8080;

constexpr std::int64_t kPollingIntervalMs = 50;

// NOLINTNEXTLINE(llvm-prefer-static-over-anonymous-namespace, cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t gShutdownRequested = 0;

// Completion signal for STDIO thread - set to true when STDIO transport ends
// NOLINTNEXTLINE(llvm-prefer-static-over-anonymous-namespace, cppcoreguidelines-avoid-non-const-global-variables)
std::atomic_bool gStdioDone {false};

// NOLINTNEXTLINE(llvm-prefer-static-over-anonymous-namespace)
auto makeTextContent(const std::string &text) -> mcp::jsonrpc::JsonValue
{
  mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
  content["type"] = "text";
  content["text"] = text;
  return content;
}

// NOLINTNEXTLINE(llvm-prefer-static-over-anonymous-namespace)
auto makeServer() -> std::shared_ptr<mcp::server::Server>
{
  mcp::lifecycle::session::ToolsCapability toolsCapability;
  toolsCapability.listChanged = true;

  mcp::lifecycle::session::ResourcesCapability resourcesCapability;
  resourcesCapability.listChanged = true;

  mcp::lifecycle::session::PromptsCapability promptsCapability;
  promptsCapability.listChanged = true;

  mcp::lifecycle::session::TasksCapability tasksCapability;
  tasksCapability.toolsCall = true;
  tasksCapability.list = true;
  tasksCapability.cancel = true;

  mcp::ServerConfiguration configuration;
  configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, resourcesCapability, toolsCapability, tasksCapability, std::nullopt);
  configuration.serverInfo = mcp::lifecycle::session::Implementation("example-dual-transport-server", "1.0.0");
  configuration.instructions = "Use tools for generated output and read resources for static context.";

  const std::shared_ptr<mcp::server::Server> server = mcp::server::Server::create(std::move(configuration));

  // Register echo tool
  mcp::ToolDefinition echoTool;
  echoTool.name = "echo";
  echoTool.description = "Echo text from arguments.text";
  echoTool.execution = mcp::jsonrpc::JsonValue::object();
  (*echoTool.execution)["taskSupport"] = "optional";
  echoTool.inputSchema = mcp::jsonrpc::JsonValue::object();
  echoTool.inputSchema["type"] = "object";
  echoTool.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();
  echoTool.inputSchema["properties"]["text"] = mcp::jsonrpc::JsonValue::object();
  echoTool.inputSchema["properties"]["text"]["type"] = "string";
  echoTool.inputSchema["required"] = mcp::jsonrpc::JsonValue::array();
  echoTool.inputSchema["required"].push_back("text");

  server->registerTool(std::move(echoTool),
                       [](const mcp::ToolCallContext &context) -> mcp::CallToolResult
                       {
                         mcp::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         result.content.push_back(makeTextContent("echo: " + context.arguments["text"].as<std::string>()));
                         return result;
                       });

  // Register delayed echo tool
  mcp::ToolDefinition delayedTool;
  delayedTool.name = "delayed_echo";
  delayedTool.description = "Sleep briefly, then echo arguments.text";
  delayedTool.execution = mcp::jsonrpc::JsonValue::object();
  (*delayedTool.execution)["taskSupport"] = "optional";
  delayedTool.inputSchema = mcp::jsonrpc::JsonValue::object();
  delayedTool.inputSchema["type"] = "object";
  delayedTool.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();
  delayedTool.inputSchema["properties"]["text"] = mcp::jsonrpc::JsonValue::object();
  delayedTool.inputSchema["properties"]["text"]["type"] = "string";
  delayedTool.inputSchema["required"] = mcp::jsonrpc::JsonValue::array();
  delayedTool.inputSchema["required"].push_back("text");

  server->registerTool(std::move(delayedTool),
                       [](const mcp::ToolCallContext &context) -> mcp::CallToolResult
                       {
                         std::this_thread::sleep_for(std::chrono::milliseconds(kDelayedEchoMilliseconds));

                         mcp::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();
                         result.content.push_back(makeTextContent("delayed: " + context.arguments["text"].as<std::string>()));
                         return result;
                       });

  // Register about resource
  mcp::ResourceDefinition resource;
  resource.uri = "resource://server/about";
  resource.name = "about";
  resource.description = "Server metadata";
  resource.mimeType = "text/plain";

  server->registerResource(
    std::move(resource),
    [](const mcp::ResourceReadContext &) -> std::vector<mcp::ResourceContent>
    {
      return {
        mcp::ResourceContent::text("resource://server/about", "Example dual-transport server with tools/resources/prompts support.", std::string("text/plain")),
      };
    });

  // Register explain-topic prompt
  mcp::PromptDefinition prompt;
  prompt.name = "explain-topic";
  prompt.description = "Generate a one-line explanatory prompt";

  mcp::PromptArgumentDefinition topicArgument;
  topicArgument.name = "topic";
  topicArgument.required = true;
  prompt.arguments.push_back(std::move(topicArgument));

  server->registerPrompt(std::move(prompt),
                         [](const mcp::PromptGetContext &context) -> mcp::PromptGetResult
                         {
                           mcp::PromptGetResult result;
                           result.description = "Single user message prompt";

                           mcp::PromptMessage message;
                           message.role = "user";
                           message.content = mcp::jsonrpc::JsonValue::object();
                           message.content["type"] = "text";
                           message.content["text"] = "Explain this topic in one paragraph: " + context.arguments["topic"].as<std::string>();
                           result.messages.push_back(std::move(message));
                           return result;
                         });

  return server;
}

// NOLINTNEXTLINE(llvm-prefer-static-over-anonymous-namespace)
auto runCombinedRunnerExample() -> void
{
  std::cerr << "=== CombinedServerRunner Example ===" << '\n';

  // Server factory - creates a fresh Server instance per call
  const mcp::server::ServerFactory serverFactory = []() -> std::shared_ptr<mcp::server::Server>
  {
    // Each session gets its own Server instance
    return makeServer();
  };

  // Configure combined runner options
  mcp::server::CombinedServerRunnerOptions options;
  options.enableStdio = true;
  options.enableHttp = true;

  // Configure HTTP transport
  options.httpOptions.transportOptions.http.endpoint.bindAddress = kHttpBind;
  options.httpOptions.transportOptions.http.endpoint.port = kHttpPort;
  options.httpOptions.transportOptions.http.endpoint.path = kHttpPath;
  options.httpOptions.transportOptions.http.requireSessionId = true;  // Multi-client isolation

  // Create and run the combined runner
  mcp::server::CombinedServerRunner runner(serverFactory, options);

  // Start HTTP in background (non-blocking)
  runner.startHttp();
  std::cerr << "HTTP server started on " << kHttpBind << ":" << runner.localPort() << kHttpPath << '\n';
  std::cerr.flush();

  // Run STDIO in a joinable thread (allows SIGINT to unblock it)
  std::cerr << "STDIO transport ready, waiting for input..." << '\n';
  std::cerr.flush();

  mcp::server::StdioServerRunner *stdioRunnerPtr = runner.stdioRunner();

  // Start STDIO in a thread and wrap to set completion flag when done
  std::thread stdioThread(
    [stdioRunnerPtr]() -> void
    {
      stdioRunnerPtr->run();
      gStdioDone = true;
    });

  // Main control loop - check for shutdown signal or STDIO completion
  while (gShutdownRequested == 0)
  {
    // Check if the STDIO thread has finished naturally
    if (gStdioDone)
    {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollingIntervalMs));
  }

  // Check what caused us to exit the loop
  if (gShutdownRequested != 0)
  {
    // SIGINT received - perform best-effort shutdown
    std::cerr << "\nReceived SIGINT, initiating graceful shutdown..." << '\n';
    std::cerr.flush();

    // Stop HTTP server immediately
    runner.stopHttp();

    // Request STDIO runner to stop
    stdioRunnerPtr->stop();

    // Best-effort unblock stdin - always set EOF bit first
    std::cin.setstate(std::ios::eofbit);

#ifndef _WIN32
    // On POSIX, also redirect stdin to /dev/null to unblock any blocking reads
    // NOLINTBEGIN(misc-include-cleaner,cppcoreguidelines-pro-type-vararg,hicpp-vararg,android-cloexec-open)
    const int devNull = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (devNull >= 0)
    {
      dup2(devNull, fileno(stdin));
      close(devNull);
    }
    // NOLINTEND(misc-include-cleaner,cppcoreguidelines-pro-type-vararg,hicpp-vararg,android-cloexec-open)
#endif

    // Now join the STDIO thread
    if (stdioThread.joinable())
    {
      stdioThread.join();
    }
  }
  else
  {
    // STDIO closed naturally
    std::cerr << "STDIO transport closed, stopping HTTP server..." << '\n';
    std::cerr.flush();
    runner.stopHttp();

    // Join the STDIO thread if still joinable
    if (stdioThread.joinable())
    {
      stdioThread.join();
    }
  }

  std::cerr << "CombinedServerRunner example complete." << '\n';
}

// NOLINTNEXTLINE(llvm-prefer-static-over-anonymous-namespace)
auto runExplicitCompositionExample() -> void
{
  std::cerr << "=== Explicit Composition Example (StdioServerRunner + StreamableHttpServerRunner) ===" << '\n';

  // Shared server factory
  const mcp::server::ServerFactory serverFactory = []() -> std::shared_ptr<mcp::server::Server> { return makeServer(); };

  // Create STDIO runner
  const mcp::server::StdioServerRunnerOptions stdioOptions;
  mcp::server::StdioServerRunner stdioRunner(serverFactory, stdioOptions);

  // Create HTTP runner
  mcp::server::StreamableHttpServerRunnerOptions httpOptions;
  httpOptions.transportOptions.http.endpoint.bindAddress = kHttpBind;
  httpOptions.transportOptions.http.endpoint.port = kHttpPort;
  httpOptions.transportOptions.http.endpoint.path = kHttpPath;
  httpOptions.transportOptions.http.requireSessionId = true;  // Multi-client isolation
  mcp::server::StreamableHttpServerRunner httpRunner(serverFactory, httpOptions);

  // Start HTTP in background
  httpRunner.start();
  std::cerr << "HTTP server started on " << kHttpBind << ":" << httpRunner.localPort() << kHttpPath << '\n';
  std::cerr.flush();

  // Run STDIO in foreground
  std::cerr << "STDIO transport ready, waiting for input..." << '\n';
  std::cerr.flush();

  stdioRunner.run();

  // Stop HTTP on exit
  std::cerr << "STDIO transport closed, stopping HTTP server..." << '\n';
  httpRunner.stop();
  std::cerr << "Explicit composition example complete." << '\n';
}

// NOLINTNEXTLINE(llvm-prefer-static-over-anonymous-namespace)
auto handleSignal(int /*signal*/) -> void
{
  // Signal handler must only perform async-signal-safe operations.
  // Setting a sig_atomic_t flag is signal-safe; logging is not.
  gShutdownRequested = 1;
}

}  // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
auto main() -> int
{
  // Set up signal handler for SIGINT
  static_cast<void>(std::signal(SIGINT, handleSignal));

  // Use the combined runner for the main example
  // This demonstrates:
  // - Shared ServerFactory building an mcp::server::Server with tools/resources/prompts
  // - Starting both transports via CombinedServerRunner
  // - HTTP configured with requireSessionId=true
  // - HTTP runs in background, STDIO runs in foreground (blocking)
  // - HTTP stopped on exit
  // - Graceful shutdown on SIGINT
  try
  {
    runCombinedRunnerExample();
  }
  catch (const std::exception &error)
  {
    std::cerr << "CombinedServerRunner example failed: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
