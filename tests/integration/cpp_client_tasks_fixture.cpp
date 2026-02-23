#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <mcp/client.hpp>
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
    clientOptions.enableGetListen = false;

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

    // Test 1: Exercise task tools exposed by the reference server
    std::string taskId;
    {
      mcp::jsonrpc::JsonValue createParams = mcp::jsonrpc::JsonValue::object();
      createParams["tool"] = "python_echo";
      createParams["arguments"] = mcp::jsonrpc::JsonValue::object();
      createParams["arguments"]["text"] = "test";

      mcp::server::CallToolResult createResult = client->callTool("tasks_create", std::move(createParams));
      if (createResult.isError)
      {
        std::cout << "tasks_create tool returned an error on reference server (continuing)" << '\n';
      }
      else
      {
        if (createResult.structuredContent.has_value() && createResult.structuredContent->is_object() && createResult.structuredContent->contains("taskId")
            && (*createResult.structuredContent)["taskId"].is_string())
        {
          taskId = (*createResult.structuredContent)["taskId"].as<std::string>();
        }

        std::cout << "tasks_create succeeded" << '\n';
      }

      mcp::server::CallToolResult listResult = client->callTool("tasks_list", mcp::jsonrpc::JsonValue::object());
      if (listResult.isError)
      {
        std::cout << "tasks_list tool returned an error on reference server (continuing)" << '\n';
      }
      else
      {
        std::cout << "tasks_list succeeded" << '\n';
      }

      if (!taskId.empty())
      {
        mcp::jsonrpc::JsonValue getParams = mcp::jsonrpc::JsonValue::object();
        getParams["taskId"] = taskId;
        mcp::server::CallToolResult getResult = client->callTool("tasks_get", std::move(getParams));
        if (getResult.isError)
        {
          std::cout << "tasks_get tool returned an error on reference server (continuing)" << '\n';
        }
        else
        {
          std::cout << "tasks_get succeeded" << '\n';
        }

        mcp::jsonrpc::JsonValue cancelParams = mcp::jsonrpc::JsonValue::object();
        cancelParams["taskId"] = taskId;
        mcp::server::CallToolResult cancelResult = client->callTool("tasks_cancel", std::move(cancelParams));
        if (cancelResult.isError)
        {
          std::cout << "tasks_cancel tool returned an error on reference server (continuing)" << '\n';
        }
        else
        {
          std::cout << "tasks_cancel succeeded" << '\n';
        }
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
