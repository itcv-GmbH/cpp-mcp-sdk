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

#include <mcp/lifecycle/session.hpp>
#include <mcp/server.hpp>
#include <mcp/server/stdio_runner.hpp>

namespace
{

constexpr std::chrono::seconds kOutboundRequestTimeout {10};
constexpr std::chrono::seconds kSessionStateWaitTimeout {15};

// Helper to create a future with a ready response
auto makeReadyResponseFuture(mcp::jsonrpc::Response response) -> std::future<mcp::jsonrpc::Response>
{
  std::promise<mcp::jsonrpc::Response> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

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
  // Test ping - simple request that should return empty result
  mcp::jsonrpc::Request pingRequest;
  pingRequest.id = std::int64_t {1};
  pingRequest.method = "ping";

  std::future<mcp::jsonrpc::Response> pingFuture = server.sendRequest(context, std::move(pingRequest));
  const mcp::jsonrpc::Response pingResponse = awaitResponse(pingFuture, "ping");
  throwIfErrorResponse(pingResponse, "ping");

  if (!std::holds_alternative<mcp::jsonrpc::SuccessResponse>(pingResponse))
  {
    throw std::runtime_error("ping did not return a success response");
  }

  // The Python client will have received the notification from logging/setLevel
  // (when it calls the tool that triggers logging)

  std::cerr << "cpp stdio integration server outbound ping assertions passed" << '\n';
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
      mcp::lifecycle::session::LoggingCapability loggingCapability;
      mcp::lifecycle::session::CompletionsCapability completionsCapability;

      mcp::server::ServerConfiguration configuration;
      configuration.capabilities = mcp::lifecycle::session::ServerCapabilities(loggingCapability,
                                                                               completionsCapability,
                                                                               std::nullopt,  // prompts
                                                                               std::nullopt,  // resources
                                                                               std::nullopt,  // tools
                                                                               std::nullopt,  // tasks
                                                                               std::nullopt  // experimental
      );

      configuration.serverInfo = mcp::lifecycle::session::Implementation("cpp-integration-stdio-server-utilities", "1.0.0");
      configuration.instructions = "STDIO integration fixture server for reference SDK utilities tests.";

      const std::shared_ptr<mcp::server::Server> server = mcp::server::Server::create(std::move(configuration));

      // Register this server in the registry
      {
        std::scoped_lock lock(serverRegistry->mutex);
        serverRegistry->servers.push_back(server);
      }

      // Override the logging/setLevel handler to emit a notification after setting the level
      server->registerRequestHandler(
        "logging/setLevel",
        [&server](const mcp::jsonrpc::RequestContext &ctx, const mcp::jsonrpc::Request &req) -> std::future<mcp::jsonrpc::Response>
        {
          // First, handle the setLevel normally - extract level from params
          if (!req.params.has_value() || !req.params->is_object())
          {
            return makeReadyResponseFuture(mcp::jsonrpc::makeErrorResponse(mcp::jsonrpc::makeInvalidParamsError(std::nullopt, "logging/setLevel requires params object"), req.id));
          }

          const auto &params = *req.params;
          if (!params.contains("level") || !params["level"].is_string())
          {
            return makeReadyResponseFuture(
              mcp::jsonrpc::makeErrorResponse(mcp::jsonrpc::makeInvalidParamsError(std::nullopt, "logging/setLevel requires string params.level"), req.id));
          }

          std::string level = params["level"].as<std::string>();

          // Emit a log message notification
          mcp::jsonrpc::Notification notification;
          notification.method = "notifications/message";
          notification.params = mcp::jsonrpc::JsonValue::object();
          (*notification.params)["level"] = level;
          (*notification.params)["logger"] = "cpp-stdio-utilities-server";
          (*notification.params)["data"] = "Log level set to " + level;

          // Send notification to client
          server->sendNotification(ctx, std::move(notification));

          // Return success response
          mcp::jsonrpc::SuccessResponse response;
          response.id = req.id;
          response.result = mcp::jsonrpc::JsonValue::object();
          return makeReadyResponseFuture(std::move(response));
        });

      // Set up completion handler for completion/complete requests
      server->setCompletionHandler(
        [](const mcp::server::CompletionRequest &request) -> mcp::server::CompletionResult
        {
          mcp::server::CompletionResult result;

          // Provide completion suggestions based on the reference type and argument value
          if (request.referenceType == mcp::server::CompletionReferenceType::kPrompt)
          {
            // For prompts, suggest some prompt names
            if (request.argumentValue.empty())
            {
              result.values.push_back("cpp_prompt_1");
              result.values.push_back("cpp_prompt_2");
            }
            else if (request.argumentValue.find("test") != std::string::npos)
            {
              result.values.push_back("test_prompt");
            }
          }
          else if (request.referenceType == mcp::server::CompletionReferenceType::kResource)
          {
            // For resources, suggest some resource URIs
            if (request.argumentValue.empty())
            {
              result.values.push_back("resource://cpp-stdio/info");
              result.values.push_back("resource://cpp-stdio/data");
            }
            else if (request.argumentValue.find("test") != std::string::npos)
            {
              result.values.push_back("test://test-resource");
            }
          }

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

          // Run the outbound assertions with an empty context
          // The runner's outbound message sender handles routing using stored session ID
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
      // The worker might still be running or not started - this is a timeout case
      std::cerr << "cpp_stdio_server_utilities_fixture failed: outbound assertions did not complete" << '\n';
      return 3;
    }

    if (!outboundAssertions.passed.load())
    {
      std::optional<std::string> failureReason;
      {
        std::scoped_lock lock(outboundAssertions.mutex);
        failureReason = outboundAssertions.failureReason;
      }

      std::cerr << "cpp_stdio_server_utilities_fixture failed: outbound assertions failed";
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
    std::cerr << "cpp_stdio_server_utilities_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
