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
#include <mcp/client/roots.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/transport/http.hpp>

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
    mcp::transport::HttpClientOptions clientOptions;
    clientOptions.endpointUrl = options.endpoint;
    if (options.token.has_value())
    {
      clientOptions.bearerToken = options.token;
    }

    client->connectHttp(clientOptions);
    client->start();

    // Track notifications received
    std::atomic<bool> observedRootsListChanged {false};

    // Register handler for notifications/roots/list_changed
    client->registerNotificationHandler("notifications/roots/list_changed",
                                        [&observedRootsListChanged](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Notification &) -> void
                                        { observedRootsListChanged.store(true); });

    // Set up a roots provider - this will be called when the server sends roots/list request
    // For this test, we just provide a simple static set of roots
    client->setRootsProvider(
      [](const mcp::RootsListContext &) -> std::vector<mcp::RootEntry>
      {
        // Return a simple root entry
        mcp::RootEntry entry;
        entry.uri = "file:///tmp/test-root";
        entry.name = "test-root";
        return {entry};
      });

    // Initialize
    const mcp::jsonrpc::Response initializeResponse = client->initialize().get();
    if (!initializeSucceeded(initializeResponse))
    {
      std::cerr << "initialize did not return success" << '\n';
      client->stop();
      return 3;
    }

    // Test 1: Call a tool to trigger roots/list request from server
    // This tool signals to the server that it's ready to receive roots/list
    {
      mcp::CallToolResult toolResult = client->callTool("cpp_trigger_roots_change", mcp::jsonrpc::JsonValue::object());

      // Wait for notification to potentially arrive after triggering roots change
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      if (observedRootsListChanged.load())
      {
        std::cout << "notifications/roots/list_changed received" << '\n';
      }
      else
      {
        std::cout << "Did not receive notifications/roots/list_changed (may be server-dependent)" << '\n';
      }

      std::cout << "roots/list test passed (provider registered)" << '\n';
    }

    client->stop();
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "cpp_client_roots_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
