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

    auto client = mcp::client::Client::create();
    mcp::transport::http::HttpClientOptions clientOptions;
    clientOptions.endpointUrl = options.endpoint;
    if (options.token.has_value())
    {
      clientOptions.bearerToken = options.token;
    }

    client->connectHttp(clientOptions);
    client->start();

    // Track notifications received
    std::atomic<bool> observedTaskStatus {false};
    std::atomic<bool> observedProgress {false};
    std::atomic<bool> observedCancelled {false};
    std::string taskStatusMessage;

    // Register handler for notifications/tasks/status
    client->registerNotificationHandler("notifications/tasks/status",
                                        [&observedTaskStatus, &taskStatusMessage](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Notification &notification) -> void
                                        {
                                          if (notification.params.has_value() && notification.params->is_object())
                                          {
                                            if (notification.params->contains("taskId"))
                                            {
                                              observedTaskStatus.store(true);
                                            }
                                            if (notification.params->contains("statusMessage"))
                                            {
                                              taskStatusMessage = (*notification.params)["statusMessage"].as<std::string>();
                                            }
                                          }
                                        });

    // Register handler for notifications/progress
    client->registerNotificationHandler("notifications/progress",
                                        [&observedProgress](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Notification &notification) -> void
                                        {
                                          if (notification.params.has_value() && notification.params->is_object() && notification.params->contains("progressToken"))
                                          {
                                            observedProgress.store(true);
                                          }
                                        });

    // Register handler for notifications/cancelled
    client->registerNotificationHandler("notifications/cancelled",
                                        [&observedCancelled](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Notification &notification) -> void
                                        {
                                          if (notification.params.has_value() && notification.params->is_object() && notification.params->contains("taskId"))
                                          {
                                            observedCancelled.store(true);
                                          }
                                        });

    // Initialize
    const mcp::jsonrpc::Response initializeResponse = client->initialize().get();
    if (!initializeSucceeded(initializeResponse))
    {
      std::cerr << "initialize did not return success" << '\n';
      client->stop();
      return 3;
    }

    // Test 1: Create a task using the tasks_create tool
    std::string taskId;
    {
      mcp::jsonrpc::JsonValue createParams = mcp::jsonrpc::JsonValue::object();
      createParams["tool"] = "python_echo";
      createParams["arguments"] = mcp::jsonrpc::JsonValue::object();
      createParams["arguments"]["text"] = "test";

      mcp::server::CallToolResult createResult = client->callTool("tasks_create", std::move(createParams));
      if (createResult.isError)
      {
        std::cerr << "tasks_create tool returned error" << '\n';
        client->stop();
        return 4;
      }

      // Extract taskId from result
      if (createResult.content.is_array() && !createResult.content.empty())
      {
        const auto &content = createResult.content[0];
        if (content.is_object() && content.contains("text"))
        {
          const std::string text = content["text"].as<std::string>();
          // Parse task ID from response (format: "Started task: task-X with duration ...")
          if (text.find("task-") != std::string::npos)
          {
            std::size_t start = text.find("task-");
            std::size_t end = text.find(" ", start);
            if (end != std::string::npos)
            {
              taskId = text.substr(start, end - start);
            }
          }
        }
      }

      if (taskId.empty())
      {
        std::cerr << "Could not extract taskId from tasks_create result" << '\n';
        client->stop();
        return 5;
      }

      std::cout << "Created task: " << taskId << '\n';
    }

    // Test 2: List tasks using tasks/list
    {
      std::future<mcp::jsonrpc::Response> listFuture = client->sendRequest("tasks/list", mcp::jsonrpc::JsonValue::object());
      mcp::jsonrpc::Response listResponse = awaitResponse(listFuture, "tasks/list");

      if (std::holds_alternative<mcp::jsonrpc::ErrorResponse>(listResponse))
      {
        const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(listResponse);
        std::cerr << "tasks/list returned error: " << error.error.message << '\n';
        client->stop();
        return 6;
      }

      std::cout << "tasks/list succeeded" << '\n';
    }

    // Test 3: Get task details using tasks/get
    {
      mcp::jsonrpc::JsonValue getParams = mcp::jsonrpc::JsonValue::object();
      getParams["taskId"] = taskId;

      std::future<mcp::jsonrpc::Response> getFuture = client->sendRequest("tasks/get", std::move(getParams));
      mcp::jsonrpc::Response getResponse = awaitResponse(getFuture, "tasks/get");

      if (std::holds_alternative<mcp::jsonrpc::ErrorResponse>(getResponse))
      {
        const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(getResponse);
        std::cerr << "tasks/get returned error: " << error.error.message << '\n';
        client->stop();
        return 7;
      }

      std::cout << "tasks/get succeeded" << '\n';
    }

    // Test 4: Wait a bit and then cancel the task using tasks/cancel
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));

      mcp::jsonrpc::JsonValue cancelParams = mcp::jsonrpc::JsonValue::object();
      cancelParams["taskId"] = taskId;

      std::future<mcp::jsonrpc::Response> cancelFuture = client->sendRequest("tasks/cancel", std::move(cancelParams));
      mcp::jsonrpc::Response cancelResponse = awaitResponse(cancelFuture, "tasks/cancel");

      if (std::holds_alternative<mcp::jsonrpc::ErrorResponse>(cancelResponse))
      {
        const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(cancelResponse);
        std::cerr << "tasks/cancel returned error: " << error.error.message << '\n';
        client->stop();
        return 8;
      }

      std::cout << "tasks/cancel succeeded" << '\n';

      // Wait a bit for the cancelled notification
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      if (observedCancelled.load())
      {
        std::cout << "notifications/cancelled received" << '\n';
      }
      else
      {
        std::cout << "Did not receive notifications/cancelled" << '\n';
      }
    }

    // Test 5: Verify we received task status notifications during task lifecycle
    if (observedTaskStatus.load())
    {
      std::cout << "notifications/tasks/status received: " << taskStatusMessage << '\n';
    }
    else
    {
      std::cout << "Did not receive notifications/tasks/status" << '\n';
    }

    if (observedProgress.load())
    {
      std::cout << "notifications/progress received" << '\n';
    }
    else
    {
      std::cout << "Did not receive notifications/progress" << '\n';
    }

    client->stop();
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "cpp_client_tasks_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
