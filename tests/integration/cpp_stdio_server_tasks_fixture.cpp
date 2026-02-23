#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <mcp/server.hpp>
#include <mcp/server/stdio_runner.hpp>
#include <mcp/util/all.hpp>
#include <mcp/util/cancellation.hpp>
#include <mcp/util/progress.hpp>

namespace
{

constexpr std::chrono::seconds kOutboundRequestTimeout {10};
constexpr std::chrono::seconds kSessionStateWaitTimeout {15};
constexpr std::int64_t kTaskProgressIntervalMs = 200;
constexpr std::int64_t kTaskTotalSteps = 5;

// Shared state between factory and fixture to track created server instances
struct ServerRegistry
{
  std::mutex mutex;
  std::vector<std::weak_ptr<mcp::server::Server>> servers;
};

struct OutboundAssertionsState
{
  std::atomic<bool> started {false};
  std::atomic<bool> completed {false};
  std::atomic<bool> passed {false};
  std::mutex mutex;
  std::optional<std::string> failureReason;
  std::thread worker;
};

auto makeTextContent(std::string text) -> mcp::jsonrpc::JsonValue
{
  mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
  content["type"] = "text";
  content["text"] = std::move(text);
  return content;
}

auto throwIfErrorResponse(const mcp::jsonrpc::Response &response, std::string_view methodName) -> void
{
  if (!std::holds_alternative<mcp::jsonrpc::ErrorResponse>(response))
  {
    return;
  }

  const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(response).error;
  throw std::runtime_error(std::string(methodName) + " failed: " + error.message);
}

auto awaitResponse(std::future<mcp::jsonrpc::Response> &future, std::string_view methodName) -> mcp::jsonrpc::Response
{
  if (future.wait_for(kOutboundRequestTimeout) != std::future_status::ready)
  {
    throw std::runtime_error(std::string(methodName) + " did not complete within timeout");
  }

  return future.get();
}

auto runOutboundAssertions(mcp::server::Server &server, const mcp::jsonrpc::RequestContext &context) -> void
{
  // Test tasks/list request
  mcp::jsonrpc::Request listRequest;
  listRequest.id = std::int64_t {4201};
  listRequest.method = "tasks/list";
  listRequest.params = mcp::jsonrpc::JsonValue::object();

  std::future<mcp::jsonrpc::Response> listFuture = server.sendRequest(context, std::move(listRequest));
  const mcp::jsonrpc::Response listResponse = awaitResponse(listFuture, "tasks/list");
  throwIfErrorResponse(listResponse, "tasks/list");

  if (!std::holds_alternative<mcp::jsonrpc::SuccessResponse>(listResponse))
  {
    throw std::runtime_error("tasks/list did not return a success response");
  }

  const auto &listResult = std::get<mcp::jsonrpc::SuccessResponse>(listResponse).result;
  if (!listResult.is_object() || !listResult.contains("tasks"))
  {
    throw std::runtime_error("tasks/list response did not include tasks array");
  }

  std::cerr << "cpp stdio integration server outbound tasks assertions passed" << '\n';
}

}  // namespace

auto main(int /*argc*/, char ** /*argv*/) -> int
{
  try
  {
    // Shared registry to track server instances created by the runner's factory
    auto serverRegistry = std::make_shared<ServerRegistry>();

    auto makeServer = [&serverRegistry]() -> std::shared_ptr<mcp::server::Server>
    {
      mcp::lifecycle::session::ToolsCapability toolsCapability;
      toolsCapability.listChanged = false;

      mcp::lifecycle::session::ResourcesCapability resourcesCapability;
      resourcesCapability.subscribe = false;
      resourcesCapability.listChanged = false;

      mcp::lifecycle::session::PromptsCapability promptsCapability;
      promptsCapability.listChanged = false;

      // Enable tasks capability
      mcp::lifecycle::session::TasksCapability tasksCapability;
      tasksCapability.list = true;
      tasksCapability.cancel = true;
      tasksCapability.toolsCall = true;  // Enable task-augmented tool calls

      mcp::lifecycle::session::ServerCapabilities capabilities(std::nullopt,  // logging
                                                               std::nullopt,  // completions
                                                               promptsCapability,  // prompts
                                                               resourcesCapability,  // resources
                                                               toolsCapability,  // tools
                                                               tasksCapability,  // tasks
                                                               std::nullopt  // experimental
      );

      mcp::server::ServerConfiguration configuration;
      configuration.capabilities = std::move(capabilities);
      configuration.serverInfo = mcp::lifecycle::session::Implementation("cpp-integration-stdio-server-tasks", "1.0.0");
      configuration.instructions = "STDIO integration fixture server for reference SDK tasks tests.";
      configuration.taskStore = std::make_shared<mcp::util::InMemoryTaskStore>();
      configuration.defaultTaskPollInterval = 1000;
      configuration.emitTaskStatusNotifications = true;

      const std::shared_ptr<mcp::server::Server> server = mcp::server::Server::create(std::move(configuration));

      // Register this server in the registry
      {
        std::scoped_lock lock(serverRegistry->mutex);
        serverRegistry->servers.push_back(server);
      }

      // Register a tool that supports task augmentation for long-running tasks
      mcp::server::ToolDefinition longRunningTaskTool;
      longRunningTaskTool.name = "cpp_long_running_task";
      longRunningTaskTool.description = "Creates a deterministic long-running task for testing";
      longRunningTaskTool.inputSchema = mcp::jsonrpc::JsonValue::object();
      longRunningTaskTool.inputSchema["type"] = "object";
      longRunningTaskTool.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();
      longRunningTaskTool.inputSchema["properties"]["steps"] = mcp::jsonrpc::JsonValue::object();
      longRunningTaskTool.inputSchema["properties"]["steps"]["type"] = "integer";
      longRunningTaskTool.inputSchema["properties"]["steps"]["description"] = "Number of progress steps (default 5)";
      longRunningTaskTool.inputSchema["properties"]["steps"]["default"] = 5;
      longRunningTaskTool.inputSchema["properties"]["text"] = mcp::jsonrpc::JsonValue::object();
      longRunningTaskTool.inputSchema["properties"]["text"]["type"] = "string";
      longRunningTaskTool.inputSchema["properties"]["text"]["description"] = "Text to process";
      longRunningTaskTool.inputSchema["required"] = mcp::jsonrpc::JsonValue::array();
      longRunningTaskTool.inputSchema["required"].push_back("text");

      server->registerTool(std::move(longRunningTaskTool),
                           [](const mcp::server::ToolCallContext &context) -> mcp::server::CallToolResult
                           {
                             // Get arguments
                             std::string text = "default";
                             std::int64_t steps = kTaskTotalSteps;

                             if (context.arguments.contains("text") && context.arguments["text"].is_string())
                             {
                               text = context.arguments["text"].as<std::string>();
                             }

                             if (context.arguments.contains("steps") && context.arguments["steps"].is_int64())
                             {
                               steps = context.arguments["steps"].as<std::int64_t>();
                             }

                             // Simulate work with progress - the server handles progress notifications automatically
                             // when task augmentation is requested
                             for (std::int64_t i = 0; i < steps; ++i)
                             {
                               std::this_thread::sleep_for(std::chrono::milliseconds(kTaskProgressIntervalMs));
                             }

                             mcp::server::CallToolResult result;
                             result.content = mcp::jsonrpc::JsonValue::array();
                             result.content.push_back(makeTextContent("Task completed: " + text + " (processed " + std::to_string(steps) + " steps)"));
                             return result;
                           });

      // Simple echo tool for basic testing
      mcp::server::ToolDefinition echoTool;
      echoTool.name = "cpp_echo";
      echoTool.description = "Echo text from arguments.text";
      echoTool.inputSchema = mcp::jsonrpc::JsonValue::object();
      echoTool.inputSchema["type"] = "object";
      echoTool.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();
      echoTool.inputSchema["properties"]["text"] = mcp::jsonrpc::JsonValue::object();
      echoTool.inputSchema["properties"]["text"]["type"] = "string";
      echoTool.inputSchema["required"] = mcp::jsonrpc::JsonValue::array();
      echoTool.inputSchema["required"].push_back("text");

      server->registerTool(std::move(echoTool),
                           [](const mcp::server::ToolCallContext &context) -> mcp::server::CallToolResult
                           {
                             mcp::server::CallToolResult result;
                             result.content = mcp::jsonrpc::JsonValue::array();
                             result.content.push_back(makeTextContent("cpp echo: " + context.arguments["text"].as<std::string>()));
                             return result;
                           });

      return server;
    };

    mcp::server::StdioServerRunnerOptions runnerOptions;
    runnerOptions.transportOptions.allowStderrLogs = true;

    mcp::server::StdioServerRunner runner(makeServer, std::move(runnerOptions));

    OutboundAssertionsState outboundAssertions;

    // Set up a polling worker to wait for session state and run outbound assertions
    outboundAssertions.worker = std::thread(
      [&runner, &serverRegistry, &outboundAssertions]() -> void
      {
        try
        {
          const auto startTime = std::chrono::steady_clock::now();
          std::shared_ptr<mcp::server::Server> targetServer;

          // Poll for a server that has reached kOperating state
          while (true)
          {
            const auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (elapsed > kSessionStateWaitTimeout)
            {
              throw std::runtime_error("timeout waiting for session to reach kOperating state");
            }

            // Find a server that has reached kOperating state
            {
              std::scoped_lock lock(serverRegistry->mutex);
              for (const auto &weakServer : serverRegistry->servers)
              {
                auto server = weakServer.lock();
                if (server)
                {
                  const auto sessionState = server->session()->state();
                  if (sessionState == mcp::lifecycle::session::SessionState::kOperating)
                  {
                    targetServer = std::move(server);
                    break;
                  }
                }
              }
            }

            if (targetServer)
            {
              break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          }

          if (!targetServer)
          {
            throw std::runtime_error("no server reached kOperating state");
          }

          // Run the outbound assertions with an empty context
          runOutboundAssertions(*targetServer, mcp::jsonrpc::RequestContext {});
          outboundAssertions.passed.store(true);
        }
        catch (const std::exception &error)
        {
          std::scoped_lock lock(outboundAssertions.mutex);
          outboundAssertions.failureReason = error.what();
        }
        catch (...)
        {
          std::scoped_lock lock(outboundAssertions.mutex);
          outboundAssertions.failureReason = "unknown outbound assertion failure";
        }

        outboundAssertions.completed.store(true);
      });

    // Run the server - blocks until EOF on stdin
    runner.run();

    if (outboundAssertions.worker.joinable())
    {
      outboundAssertions.worker.join();
    }

    // Check if outbound assertions were started
    if (!outboundAssertions.completed.load())
    {
      std::cerr << "cpp_stdio_server_tasks_fixture failed: outbound tasks assertions did not complete" << '\n';
      return 3;
    }

    if (!outboundAssertions.passed.load())
    {
      std::optional<std::string> failureReason;
      {
        std::scoped_lock lock(outboundAssertions.mutex);
        failureReason = outboundAssertions.failureReason;
      }

      std::cerr << "cpp_stdio_server_tasks_fixture failed: outbound tasks assertions failed";
      if (failureReason.has_value())
      {
        std::cerr << " (" << *failureReason << ')';
      }
      std::cerr << '\n';
      return 3;
    }

    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "cpp_stdio_server_tasks_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
