#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <iostream>
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

namespace
{

constexpr std::chrono::seconds kOutboundRequestTimeout {10};
constexpr std::chrono::seconds kSessionStateWaitTimeout {15};
constexpr std::int64_t kRootsListRequestId = 4201;

struct RootsAssertionsState
{
  std::atomic<bool> started {false};
  std::atomic<bool> completed {false};
  std::atomic<bool> passed {false};
  std::atomic<bool> rootsListChangedReceived {false};
  std::mutex mutex;
  std::optional<std::string> failureReason;
  std::thread worker;
};

// Shared state between factory and fixture to track created server instances
struct ServerRegistry
{
  std::mutex mutex;
  std::vector<std::weak_ptr<mcp::server::Server>> servers;
};

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

auto runRootsAssertions(mcp::server::Server &server, const mcp::jsonrpc::RequestContext &context) -> void
{
  // Send roots/list request to the client
  mcp::jsonrpc::Request rootsListRequest;
  rootsListRequest.id = std::int64_t {kRootsListRequestId};
  rootsListRequest.method = "roots/list";
  rootsListRequest.params = mcp::jsonrpc::JsonValue::object();

  std::future<mcp::jsonrpc::Response> rootsListFuture = server.sendRequest(context, std::move(rootsListRequest));
  const mcp::jsonrpc::Response rootsListResponse = awaitResponse(rootsListFuture, "roots/list");
  throwIfErrorResponse(rootsListResponse, "roots/list");

  if (!std::holds_alternative<mcp::jsonrpc::SuccessResponse>(rootsListResponse))
  {
    throw std::runtime_error("roots/list did not return a success response");
  }

  const auto &rootsResult = std::get<mcp::jsonrpc::SuccessResponse>(rootsListResponse).result;

  // Validate response structure
  if (!rootsResult.is_object() || !rootsResult.contains("roots") || !rootsResult["roots"].is_array())
  {
    throw std::runtime_error("roots/list response did not contain roots array");
  }

  const auto &roots = rootsResult["roots"];
  if (roots.size() == 0)
  {
    throw std::runtime_error("roots/list returned no roots - expected at least one root from client");
  }

  // Validate at least one root has required fields
  bool foundValidRoot = false;
  for (std::size_t i = 0; i < roots.size(); ++i)
  {
    const auto &root = roots[i];
    if (!root.is_object())
    {
      continue;
    }

    if (!root.contains("uri") || !root["uri"].is_string())
    {
      continue;
    }

    // Check URI scheme - must be file://
    const std::string uri = root["uri"].as<std::string>();
    if (uri.rfind("file://", 0) == 0)
    {
      foundValidRoot = true;
      break;
    }
  }

  if (!foundValidRoot)
  {
    throw std::runtime_error("roots/list did not return any roots with file:// URI scheme");
  }

  std::cerr << "cpp stdio integration server roots/list assertions passed (received " << roots.size() << " root(s))" << '\n';
}

}  // namespace

auto main(int /*argc*/, char ** /*argv*/) -> int
{
  try
  {
    // Shared registry to track server instances created by the runner's factory
    auto serverRegistry = std::make_shared<ServerRegistry>();

    // Shared state for assertions (passed to makeServer for notification handler access)
    auto assertionsState = std::make_shared<RootsAssertionsState>();

    auto makeServer = [&serverRegistry, &assertionsState]() -> std::shared_ptr<mcp::server::Server>
    {
      mcp::lifecycle::session::ToolsCapability toolsCapability;
      mcp::lifecycle::session::ResourcesCapability resourcesCapability;
      mcp::lifecycle::session::PromptsCapability promptsCapability;

      mcp::server::ServerConfiguration configuration;
      configuration.capabilities =
        mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, resourcesCapability, toolsCapability, std::nullopt, std::nullopt);
      configuration.serverInfo = mcp::lifecycle::session::Implementation("cpp-integration-stdio-server-roots", "1.0.0");
      configuration.instructions = "STDIO integration fixture server for reference SDK roots tests.";

      const std::shared_ptr<mcp::server::Server> server = mcp::server::Server::create(std::move(configuration));

      // Register this server in the registry
      {
        std::scoped_lock lock(serverRegistry->mutex);
        serverRegistry->servers.push_back(server);
      }

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
                             mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
                             content["type"] = "text";
                             content["text"] = "cpp echo: " + context.arguments["text"].as<std::string>();
                             result.content.push_back(std::move(content));
                             return result;
                           });

      mcp::server::ResourceDefinition infoResource;
      infoResource.uri = "resource://cpp-stdio-server-roots/info";
      infoResource.name = "cpp-stdio-server-roots-info";
      infoResource.description = "Reference data exposed by the C++ STDIO roots integration fixture";
      infoResource.mimeType = "text/plain";

      server->registerResource(std::move(infoResource),
                               [](const mcp::server::ResourceReadContext &) -> std::vector<mcp::server::ResourceContent>
                               {
                                 return {
                                   mcp::server::ResourceContent::text("resource://cpp-stdio-server-roots/info", "cpp stdio roots integration resource", std::string("text/plain")),
                                 };
                               });

      mcp::server::PromptDefinition prompt;
      prompt.name = "cpp_stdio_server_roots_prompt";
      prompt.description = "Returns a prompt with the provided topic";

      mcp::server::PromptArgumentDefinition topicArgument;
      topicArgument.name = "topic";
      topicArgument.required = true;
      prompt.arguments.push_back(std::move(topicArgument));

      server->registerPrompt(std::move(prompt),
                             [](const mcp::server::PromptGetContext &context) -> mcp::server::PromptGetResult
                             {
                               mcp::server::PromptGetResult result;
                               result.description = "C++ STDIO roots integration prompt";

                               mcp::server::PromptMessage message;
                               message.role = "user";
                               message.content = mcp::jsonrpc::JsonValue::object();
                               message.content["type"] = "text";
                               message.content["text"] = "C++ stdio roots prompt topic: " + context.arguments["topic"].as<std::string>();
                               result.messages.push_back(std::move(message));
                               return result;
                             });

      // Register notification handler for roots/list_changed
      server->registerNotificationHandler("notifications/roots/list_changed",
                                          [&assertionsState](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Notification &)
                                          {
                                            std::scoped_lock lock(assertionsState->mutex);
                                            assertionsState->rootsListChangedReceived.store(true);
                                            std::cerr << "MARKER: NOTIFICATION_ROOTS_LIST_CHANGED_RECEIVED" << std::endl;
                                          });

      // Register a tool for Python client to signal root change
      mcp::server::ToolDefinition triggerTool;
      triggerTool.name = "cpp_trigger_roots_change";
      triggerTool.description = "Signal that client will mutate roots";

      server->registerTool(std::move(triggerTool),
                           [](const mcp::server::ToolCallContext &) -> mcp::server::CallToolResult
                           {
                             // Client will now mutate its roots and emit notification
                             mcp::server::CallToolResult result;
                             result.content = mcp::jsonrpc::JsonValue::array();
                             mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
                             content["type"] = "text";
                             content["text"] = "Trigger roots change signal sent";
                             result.content.push_back(std::move(content));
                             return result;
                           });

      return server;
    };

    mcp::server::StdioServerRunnerOptions runnerOptions;
    runnerOptions.transportOptions.allowStderrLogs = true;

    mcp::server::StdioServerRunner runner(makeServer, std::move(runnerOptions));

    // Set up a polling worker to wait for session state and run roots assertions
    // The worker polls for the first session to reach kOperating state (after notifications/initialized)
    assertionsState->worker = std::thread(
      [&runner, &serverRegistry, &assertionsState]() -> void
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

          // Run the roots assertions with an empty context
          // The runner's outbound message sender handles routing using stored session ID
          runRootsAssertions(*targetServer, mcp::jsonrpc::RequestContext {});
          assertionsState->passed.store(true);
        }
        catch (const std::exception &error)
        {
          std::scoped_lock lock(assertionsState->mutex);
          assertionsState->failureReason = error.what();
        }
        catch (...)
        {
          std::scoped_lock lock(assertionsState->mutex);
          assertionsState->failureReason = "unknown roots assertion failure";
        }

        assertionsState->completed.store(true);
      });

    // Run the server - blocks until EOF on stdin
    runner.run();

    if (assertionsState->worker.joinable())
    {
      assertionsState->worker.join();
    }

    // Check if roots assertions were started
    if (!assertionsState->completed.load())
    {
      // The worker might still be running or not started - this is a timeout case
      std::cerr << "cpp_stdio_server_roots_fixture failed: roots assertions did not complete" << '\n';
      return 3;
    }

    if (!assertionsState->passed.load())
    {
      std::optional<std::string> failureReason;
      {
        std::scoped_lock lock(assertionsState->mutex);
        failureReason = assertionsState->failureReason;
      }

      std::cerr << "cpp_stdio_server_roots_fixture failed: roots assertions failed";
      if (failureReason.has_value())
      {
        std::cerr << " (" << *failureReason << ')';
      }
      std::cerr << '\n';
      return 3;
    }

    // Validate that we received the roots/list_changed notification
    if (!assertionsState->rootsListChangedReceived.load())
    {
      std::scoped_lock lock(assertionsState->mutex);
      assertionsState->failureReason = "notifications/roots/list_changed was not received";
      assertionsState->passed.store(false);

      std::cerr << "cpp_stdio_server_roots_fixture failed: notifications/roots/list_changed was not received" << '\n';
      return 3;
    }

    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "cpp_stdio_server_roots_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
