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

#include <mcp/server/server.hpp>
#include <mcp/server/stdio_runner.hpp>

namespace
{

constexpr std::chrono::seconds kOutboundRequestTimeout {10};
constexpr std::chrono::seconds kSessionStateWaitTimeout {15};

struct OutboundAssertionsState
{
  std::atomic<bool> started {false};
  std::atomic<bool> completed {false};
  std::atomic<bool> passed {false};
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

auto makeTextContent(std::string text) -> mcp::jsonrpc::JsonValue
{
  mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
  content["type"] = "text";
  content["text"] = std::move(text);
  return content;
}

auto runOutboundAssertions(mcp::server::Server &server) -> void
{
  // Test 1: List resource templates - verify templates are registered
  {
    mcp::jsonrpc::Request request;
    request.id = std::int64_t {1};
    request.method = "resources/templates/list";
    request.params = mcp::jsonrpc::JsonValue::object();

    // Synchronously handle the request
    auto future = server.handleRequest(mcp::jsonrpc::RequestContext {}, request);
    if (future.wait_for(kOutboundRequestTimeout) != std::future_status::ready)
    {
      throw std::runtime_error("resources/templates/list did not complete within timeout");
    }

    mcp::jsonrpc::Response response = future.get();
    if (!std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response))
    {
      throw std::runtime_error("resources/templates/list did not return a success response");
    }

    const auto &success = std::get<mcp::jsonrpc::SuccessResponse>(response);
    if (!success.result.is_object() || !success.result.contains("resourceTemplates"))
    {
      throw std::runtime_error("resources/templates/list response missing resourceTemplates");
    }

    const auto &templates = success.result["resourceTemplates"];
    if (!templates.is_array())
    {
      throw std::runtime_error("resources/templates/list resourceTemplates is not an array");
    }

    // Verify at least one template exists
    if (templates.size() < 1)
    {
      throw std::runtime_error("resources/templates/list should have at least one template");
    }

    // Verify template structure
    bool foundUriTemplate = false;
    for (std::size_t i = 0; i < templates.size(); ++i)
    {
      const auto &t = templates[i];
      if (t.is_object() && t.contains("uriTemplate"))
      {
        foundUriTemplate = true;
        break;
      }
    }

    if (!foundUriTemplate)
    {
      throw std::runtime_error("No template with uriTemplate found");
    }

    std::cerr << "resources/templates/list passed" << '\n';
  }

  // Test 2: Subscribe to a resource
  {
    mcp::jsonrpc::Request request;
    request.id = std::int64_t {2};
    request.method = "resources/subscribe";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["uri"] = "resource://cpp-stdio-server/user/{userId}";

    mcp::jsonrpc::RequestContext context;
    context.sessionId = "test-session";

    auto future = server.handleRequest(context, request);
    if (future.wait_for(kOutboundRequestTimeout) != std::future_status::ready)
    {
      throw std::runtime_error("resources/subscribe did not complete within timeout");
    }

    mcp::jsonrpc::Response response = future.get();
    if (!std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response))
    {
      throw std::runtime_error("resources/subscribe did not return a success response");
    }

    std::cerr << "resources/subscribe passed" << '\n';
  }

  // Test 3: Unsubscribe from a resource
  {
    mcp::jsonrpc::Request request;
    request.id = std::int64_t {3};
    request.method = "resources/unsubscribe";
    request.params = mcp::jsonrpc::JsonValue::object();
    (*request.params)["uri"] = "resource://cpp-stdio-server/user/{userId}";

    mcp::jsonrpc::RequestContext context;
    context.sessionId = "test-session";

    auto future = server.handleRequest(context, request);
    if (future.wait_for(kOutboundRequestTimeout) != std::future_status::ready)
    {
      throw std::runtime_error("resources/unsubscribe did not complete within timeout");
    }

    mcp::jsonrpc::Response response = future.get();
    if (!std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response))
    {
      throw std::runtime_error("resources/unsubscribe did not return a success response");
    }

    std::cerr << "resources/unsubscribe passed" << '\n';
  }

  // Test 4: Emit notifications/resources/updated
  {
    mcp::jsonrpc::RequestContext context;
    context.sessionId = "test-session";

    // First subscribe
    mcp::jsonrpc::Request subscribeRequest;
    subscribeRequest.id = std::int64_t {4};
    subscribeRequest.method = "resources/subscribe";
    subscribeRequest.params = mcp::jsonrpc::JsonValue::object();
    (*subscribeRequest.params)["uri"] = "resource://cpp-stdio-server/dynamic";

    auto subscribeFuture = server.handleRequest(context, subscribeRequest);
    subscribeFuture.get();

    // Now send update notification
    bool notifyResult = server.notifyResourceUpdated("resource://cpp-stdio-server/dynamic", context);
    if (!notifyResult)
    {
      throw std::runtime_error("notifyResourceUpdated returned false");
    }

    std::cerr << "notifications/resources/updated passed" << '\n';
  }

  // Test 5: Emit notifications/resources/list_changed
  {
    mcp::jsonrpc::RequestContext context;

    bool notifyResult = server.notifyResourcesListChanged(context);
    if (!notifyResult)
    {
      throw std::runtime_error("notifyResourcesListChanged returned false");
    }

    std::cerr << "notifications/resources/list_changed passed" << '\n';
  }

  std::cerr << "cpp stdio integration server resources advanced assertions passed" << '\n';
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
      mcp::lifecycle::session::ResourcesCapability resourcesCapability;
      resourcesCapability.subscribe = true;
      resourcesCapability.listChanged = true;
      mcp::lifecycle::session::PromptsCapability promptsCapability;

      mcp::ServerConfiguration configuration;
      configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, resourcesCapability, toolsCapability, std::nullopt, std::nullopt);
      configuration.serverInfo = mcp::lifecycle::session::Implementation("cpp-integration-stdio-server-resources", "1.0.0");
      configuration.instructions = "STDIO integration fixture server for reference SDK tests - resources advanced.";

      const std::shared_ptr<mcp::server::Server> server = mcp::server::Server::create(std::move(configuration));

      // Register this server in the registry
      {
        std::scoped_lock lock(serverRegistry->mutex);
        serverRegistry->servers.push_back(server);
      }

      // Register a regular resource
      mcp::ResourceDefinition infoResource;
      infoResource.uri = "resource://cpp-stdio-server/info";
      infoResource.name = "cpp-stdio-server-info";
      infoResource.description = "Reference data exposed by the C++ STDIO integration fixture";
      infoResource.mimeType = "text/plain";

      server->registerResource(std::move(infoResource),
                               [](const mcp::ResourceReadContext &) -> std::vector<mcp::ResourceContent>
                               {
                                 return {
                                   mcp::ResourceContent::text("resource://cpp-stdio-server/info", "cpp stdio integration resource", std::string("text/plain")),
                                 };
                               });

      // Register a dynamic resource for subscription tests
      mcp::ResourceDefinition dynamicResource;
      dynamicResource.uri = "resource://cpp-stdio-server/dynamic";
      dynamicResource.name = "cpp-stdio-server-dynamic";
      dynamicResource.description = "Dynamic resource for subscription testing";
      dynamicResource.mimeType = "text/plain";

      server->registerResource(std::move(dynamicResource),
                               [](const mcp::ResourceReadContext &) -> std::vector<mcp::ResourceContent>
                               {
                                 return {
                                   mcp::ResourceContent::text("resource://cpp-stdio-server/dynamic", "dynamic content", std::string("text/plain")),
                                 };
                               });

      // Register resource template: user profile
      mcp::ResourceTemplateDefinition userProfileTemplate;
      userProfileTemplate.uriTemplate = "resource://cpp-stdio-server/user/{userId}";
      userProfileTemplate.name = "cpp-stdio-server-user-profile";
      userProfileTemplate.description = "User profile resource with userId template parameter";
      userProfileTemplate.mimeType = "application/json";

      server->registerResourceTemplate(std::move(userProfileTemplate));

      // Register resource template: system status
      mcp::ResourceTemplateDefinition systemStatusTemplate;
      systemStatusTemplate.uriTemplate = "resource://cpp-stdio-server/system/{systemId}";
      systemStatusTemplate.name = "cpp-stdio-server-system-status";
      systemStatusTemplate.description = "System status resource with systemId template parameter";
      systemStatusTemplate.mimeType = "application/json";

      server->registerResourceTemplate(std::move(systemStatusTemplate));

      // Register tool to trigger resource updates (for testing notifications)
      mcp::ToolDefinition updateTool;
      updateTool.name = "cpp_update_resource";
      updateTool.description = "Trigger a resource update notification";
      updateTool.inputSchema = mcp::jsonrpc::JsonValue::object();
      updateTool.inputSchema["type"] = "object";
      updateTool.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();
      updateTool.inputSchema["properties"]["uri"] = mcp::jsonrpc::JsonValue::object();
      updateTool.inputSchema["properties"]["uri"]["type"] = "string";
      updateTool.inputSchema["required"] = mcp::jsonrpc::JsonValue::array();
      updateTool.inputSchema["required"].push_back("uri");

      server->registerTool(std::move(updateTool),
                           [](const mcp::ToolCallContext &context) -> mcp::CallToolResult
                           {
                             mcp::CallToolResult result;
                             result.content = mcp::jsonrpc::JsonValue::array();

                             const std::string &uri = context.arguments["uri"].as<std::string>();
                             result.content.push_back(makeTextContent("updated resource: " + uri));

                             return result;
                           });

      return server;
    };

    mcp::server::StdioServerRunnerOptions runnerOptions;
    runnerOptions.transportOptions.allowStderrLogs = true;

    mcp::server::StdioServerRunner runner(makeServer, std::move(runnerOptions));

    OutboundAssertionsState outboundAssertions;

    // Set up a polling worker to wait for session state and run outbound assertions
    // The worker polls for the first session to reach kOperating state (after notifications/initialized)
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

          // Run the outbound assertions
          runOutboundAssertions(*targetServer);
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
      // The worker might still be running or not started - this is a timeout case
      std::cerr << "cpp_stdio_server_resources_advanced_fixture failed: outbound resources assertions did not complete" << '\n';
      return 3;
    }

    if (!outboundAssertions.passed.load())
    {
      std::optional<std::string> failureReason;
      {
        std::scoped_lock lock(outboundAssertions.mutex);
        failureReason = outboundAssertions.failureReason;
      }

      std::cerr << "cpp_stdio_server_resources_advanced_fixture failed: outbound resources assertions failed";
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
    std::cerr << "cpp_stdio_server_resources_advanced_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
