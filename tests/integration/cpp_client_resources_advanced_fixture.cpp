#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <mcp/client/client.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/transport/all.hpp>

namespace
{

struct Options
{
  std::string endpoint;
  std::optional<std::string> token;
};

constexpr std::chrono::seconds kRequestTimeout {10};

auto parseOptions(int argc, char **argv) -> Options
{
  Options options;

  std::vector<std::string> arguments;
  arguments.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
  for (int index = 1; index < argc; ++index)
  {
    arguments.emplace_back(argv[index]);
  }

  for (std::size_t index = 0; index < arguments.size(); ++index)
  {
    const std::string_view argument = arguments[index];
    const auto requireValue = [&arguments, &index](std::string_view name) -> std::string
    {
      if (index + 1 >= arguments.size())
      {
        throw std::invalid_argument("Missing value for argument: " + std::string(name));
      }

      ++index;
      return arguments[index];
    };

    if (argument == "--endpoint")
    {
      options.endpoint = requireValue(argument);
      continue;
    }

    if (argument == "--token")
    {
      options.token = requireValue(argument);
      continue;
    }

    throw std::invalid_argument("Unknown argument: " + std::string(argument));
  }

  if (options.endpoint.empty())
  {
    throw std::invalid_argument("--endpoint is required");
  }

  return options;
}

auto initializeSucceeded(const mcp::jsonrpc::Response &response) -> bool
{
  return std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response);
}

auto awaitResponse(std::future<mcp::jsonrpc::Response> &future, std::string_view methodName) -> mcp::jsonrpc::Response
{
  if (future.wait_for(kRequestTimeout) != std::future_status::ready)
  {
    throw std::runtime_error(std::string(methodName) + " did not complete within timeout");
  }

  return future.get();
}

}  // namespace

auto main(int argc, char **argv) -> int
{
  try
  {
    const Options options = parseOptions(argc, argv);

    auto client = mcp::Client::create();
    mcp::transport::http::HttpClientOptions clientOptions;
    clientOptions.endpointUrl = options.endpoint;
    if (options.token.has_value())
    {
      clientOptions.bearerToken = options.token;
    }

    client->connectHttp(clientOptions);
    client->start();

    // Track notifications received
    std::atomic<bool> observedResourceUpdated {false};
    std::atomic<bool> observedResourcesListChanged {false};

    // Register handler for notifications/resources/updated
    client->registerNotificationHandler("notifications/resources/updated",
                                        [&observedResourceUpdated](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Notification &notification) -> void
                                        {
                                          if (notification.params.has_value() && notification.params->is_object() && notification.params->contains("uri"))
                                          {
                                            observedResourceUpdated.store(true);
                                          }
                                        });

    // Register handler for notifications/resources/list_changed
    client->registerNotificationHandler("notifications/resources/list_changed",
                                        [&observedResourcesListChanged](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Notification &) -> void
                                        { observedResourcesListChanged.store(true); });

    // Initialize
    const mcp::jsonrpc::Response initializeResponse = client->initialize().get();
    if (!initializeSucceeded(initializeResponse))
    {
      std::cerr << "initialize did not return success" << '\n';
      client->stop();
      return 3;
    }

    // Test 1: List resource templates
    {
      mcp::ListResourceTemplatesResult templatesResult = client->listResourceTemplates();

      // Check if templates are present - the Python reference server should have one
      std::cout << "listResourceTemplates returned " << templatesResult.resourceTemplates.size() << " template(s)" << '\n';
      std::cout << "resources/templates/list test passed" << '\n';
    }

    // Test 2: Subscribe and unsubscribe to a resource
    {
      // First, list resources to get a valid URI
      mcp::ListResourcesResult resourcesResult = client->listResources();
      if (resourcesResult.resources.empty())
      {
        std::cerr << "No resources available to subscribe to" << '\n';
        client->stop();
        return 4;
      }

      // Subscribe to the first resource
      mcp::jsonrpc::JsonValue subscribeParams = mcp::jsonrpc::JsonValue::object();
      subscribeParams["uri"] = resourcesResult.resources[0].uri;

      std::future<mcp::jsonrpc::Response> subscribeFuture = client->sendRequest("resources/subscribe", std::move(subscribeParams));
      mcp::jsonrpc::Response subscribeResponse = awaitResponse(subscribeFuture, "resources/subscribe");

      if (std::holds_alternative<mcp::jsonrpc::ErrorResponse>(subscribeResponse))
      {
        const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(subscribeResponse);
        std::cerr << "resources/subscribe returned error: " << error.error.message << '\n';
        client->stop();
        return 5;
      }

      std::cout << "resources/subscribe succeeded" << '\n';

      // Unsubscribe
      mcp::jsonrpc::JsonValue unsubscribeParams = mcp::jsonrpc::JsonValue::object();
      unsubscribeParams["uri"] = resourcesResult.resources[0].uri;

      std::future<mcp::jsonrpc::Response> unsubscribeFuture = client->sendRequest("resources/unsubscribe", std::move(unsubscribeParams));
      mcp::jsonrpc::Response unsubscribeResponse = awaitResponse(unsubscribeFuture, "resources/unsubscribe");

      if (std::holds_alternative<mcp::jsonrpc::ErrorResponse>(unsubscribeResponse))
      {
        const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(unsubscribeResponse);
        std::cerr << "resources/unsubscribe returned error: " << error.error.message << '\n';
        client->stop();
        return 6;
      }

      std::cout << "resources/unsubscribe succeeded" << '\n';
    }

    // Test 3: Wait for notifications
    // The test will call a tool to emit these notifications
    {
      // Call a tool to trigger resource updated notification
      mcp::jsonrpc::JsonValue emitParams = mcp::jsonrpc::JsonValue::object();
      emitParams["uri"] = "resource://python-server/info";

      mcp::server::CallToolResult emitResult = client->callTool("emit_resource_updated", std::move(emitParams));
      if (emitResult.isError)
      {
        std::cerr << "emit_resource_updated tool returned error" << '\n';
        client->stop();
        return 7;
      }

      // Wait a bit for the notification to potentially arrive
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      if (!observedResourceUpdated.load())
      {
        std::cerr << "Did not receive notifications/resources/updated" << '\n';
        client->stop();
        return 8;
      }

      std::cout << "notifications/resources/updated test passed" << '\n';
    }

    // Test 4: Trigger list changed notification
    {
      mcp::server::CallToolResult emitListChangedResult = client->callTool("emit_resources_list_changed", mcp::jsonrpc::JsonValue::object());
      if (emitListChangedResult.isError)
      {
        std::cerr << "emit_resources_list_changed tool returned error" << '\n';
        client->stop();
        return 9;
      }

      // Wait a bit for the notification to potentially arrive
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      if (!observedResourcesListChanged.load())
      {
        std::cerr << "Did not receive notifications/resources/list_changed" << '\n';
        client->stop();
        return 10;
      }

      std::cout << "notifications/resources/list_changed test passed" << '\n';
    }

    client->stop();
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "cpp_client_resources_advanced_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
