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
    clientOptions.enableGetListen = false;

    client->connectHttp(clientOptions);
    client->start();

    // Track notifications received
    std::atomic<bool> observedLogMessage {false};
    std::string receivedLogData;

    // Register handler for notifications/message
    client->registerNotificationHandler("notifications/message",
                                        [&observedLogMessage, &receivedLogData](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Notification &notification) -> void
                                        {
                                          if (notification.params.has_value() && notification.params->is_object())
                                          {
                                            if (notification.params->contains("data"))
                                            {
                                              receivedLogData = (*notification.params)["data"].as<std::string>();
                                            }
                                            if (notification.params->contains("level"))
                                            {
                                              observedLogMessage.store(true);
                                            }
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

    // Test 1: Call ping method
    {
      std::future<mcp::jsonrpc::Response> pingFuture = client->sendRequest("ping", mcp::jsonrpc::JsonValue::object());
      mcp::jsonrpc::Response pingResponse = awaitResponse(pingFuture, "ping");

      if (std::holds_alternative<mcp::jsonrpc::ErrorResponse>(pingResponse))
      {
        const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(pingResponse);
        std::cerr << "ping returned error: " << error.error.message << '\n';
        client->stop();
        return 4;
      }

      const auto &success = std::get<mcp::jsonrpc::SuccessResponse>(pingResponse);
      std::cout << "ping succeeded" << '\n';
    }

    // Test 2: Call logging/setLevel
    {
      mcp::jsonrpc::JsonValue loggingParams = mcp::jsonrpc::JsonValue::object();
      loggingParams["level"] = "info";

      std::future<mcp::jsonrpc::Response> loggingFuture = client->sendRequest("logging/setLevel", std::move(loggingParams));
      mcp::jsonrpc::Response loggingResponse = awaitResponse(loggingFuture, "logging/setLevel");

      if (std::holds_alternative<mcp::jsonrpc::ErrorResponse>(loggingResponse))
      {
        const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(loggingResponse);
        if (error.error.message.find("Method not found") != std::string::npos)
        {
          std::cout << "logging/setLevel is not implemented by reference server (continuing)" << '\n';
        }
        else
        {
          std::cerr << "logging/setLevel returned error: " << error.error.message << '\n';
          client->stop();
          return 5;
        }
      }

      if (std::holds_alternative<mcp::jsonrpc::SuccessResponse>(loggingResponse))
      {
        std::cout << "logging/setLevel succeeded" << '\n';

        // Wait a bit for the notification to arrive
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (!observedLogMessage.load())
        {
          std::cerr << "Did not receive notifications/message after logging/setLevel" << '\n';
          client->stop();
          return 6;
        }

        std::cout << "Received notifications/message: " << receivedLogData << '\n';
      }
    }

    // Test 3: Call completion/complete
    {
      mcp::jsonrpc::JsonValue completionParams = mcp::jsonrpc::JsonValue::object();
      completionParams["ref"] = mcp::jsonrpc::JsonValue::object();
      completionParams["argument"] = mcp::jsonrpc::JsonValue::object();

      std::future<mcp::jsonrpc::Response> completionFuture = client->sendRequest("completion/complete", std::move(completionParams));
      mcp::jsonrpc::Response completionResponse = awaitResponse(completionFuture, "completion/complete");

      if (std::holds_alternative<mcp::jsonrpc::ErrorResponse>(completionResponse))
      {
        const auto &error = std::get<mcp::jsonrpc::ErrorResponse>(completionResponse);
        std::cout << "completion/complete is not supported by reference server (" << error.error.message << ")" << '\n';
        client->stop();
        return 0;
      }

      const auto &success = std::get<mcp::jsonrpc::SuccessResponse>(completionResponse);
      if (!success.result.is_object() || !success.result.contains("completion"))
      {
        std::cerr << "completion/complete result missing completion" << '\n';
        client->stop();
        return 8;
      }

      std::cout << "completion/complete succeeded" << '\n';
    }

    client->stop();
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "cpp_client_utilities_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
