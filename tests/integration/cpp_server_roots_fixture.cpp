#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <iostream>
#include <limits>
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
#include <mcp/server/streamable_http_runner.hpp>

namespace
{

struct Options
{
  std::string bindAddress = "127.0.0.1";
  std::uint16_t port = 0;
  std::string path = "/mcp";
  std::string bearerToken = "integration-token";
};

constexpr std::chrono::seconds kOutboundRequestTimeout {10};
constexpr std::chrono::seconds kSessionStateWaitTimeout {15};
constexpr std::int64_t kRootsListRequestId = 4201;
constexpr std::string_view kTriggerRootsChangeToolName = "cpp_trigger_roots_change";

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
  std::vector<std::weak_ptr<mcp::Server>> servers;
};

class StaticTokenVerifier final : public mcp::auth::OAuthTokenVerifier
{
public:
  explicit StaticTokenVerifier(std::string expectedToken)
    : expectedToken_(std::move(expectedToken))
  {
  }

  auto verifyToken(const mcp::auth::OAuthTokenVerificationRequest &request) const -> mcp::auth::OAuthTokenVerificationResult override
  {
    mcp::auth::OAuthTokenVerificationResult result;
    if (request.bearerToken != expectedToken_)
    {
      return result;
    }

    result.status = mcp::auth::OAuthTokenVerificationStatus::kValid;
    result.audienceBound = true;
    result.authorizationContext.taskIsolationKey = "cpp-integration-user";
    result.authorizationContext.subject = "cpp-integration-user";
    result.authorizationContext.grantedScopes.values = {
      "mcp:read",
    };
    return result;
  }

private:
  std::string expectedToken_;
};

auto parsePort(const std::string &value) -> std::uint16_t
{
  const auto parsed = static_cast<std::uint64_t>(std::strtoull(value.c_str(), nullptr, 10));  // NOLINT(cert-err34-c)
  if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max()))
  {
    throw std::invalid_argument("port must be <= 65535");
  }

  return static_cast<std::uint16_t>(parsed);
}

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

    if (argument == "--bind")
    {
      options.bindAddress = requireValue(argument);
      continue;
    }

    if (argument == "--port")
    {
      options.port = parsePort(requireValue(argument));
      continue;
    }

    if (argument == "--path")
    {
      options.path = requireValue(argument);
      continue;
    }

    if (argument == "--token")
    {
      options.bearerToken = requireValue(argument);
      continue;
    }

    throw std::invalid_argument("Unknown argument: " + std::string(argument));
  }

  return options;
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

auto runRootsAssertions(mcp::Server &server, const mcp::jsonrpc::RequestContext &context, RootsAssertionsState &assertionsState) -> void
{
  // Send roots/list request to the client
  mcp::jsonrpc::Request rootsRequest;
  rootsRequest.id = std::int64_t {kRootsListRequestId};
  rootsRequest.method = "roots/list";
  rootsRequest.params = mcp::jsonrpc::JsonValue::object();

  std::future<mcp::jsonrpc::Response> rootsFuture = server.sendRequest(context, std::move(rootsRequest));
  const mcp::jsonrpc::Response rootsResponse = awaitResponse(rootsFuture, "roots/list");
  throwIfErrorResponse(rootsResponse, "roots/list");

  if (!std::holds_alternative<mcp::jsonrpc::SuccessResponse>(rootsResponse))
  {
    throw std::runtime_error("roots/list did not return a success response");
  }

  const auto &rootsResult = std::get<mcp::jsonrpc::SuccessResponse>(rootsResponse).result;
  if (!rootsResult.is_object())
  {
    throw std::runtime_error("roots/list response result is not an object");
  }

  // Validate response contains "roots" array with at least one root
  if (!rootsResult.contains("roots"))
  {
    throw std::runtime_error("roots/list response does not contain 'roots' key");
  }

  const auto &roots = rootsResult["roots"];
  if (!roots.is_array())
  {
    throw std::runtime_error("roots/list response 'roots' is not an array");
  }

  if (roots.empty())
  {
    throw std::runtime_error("roots/list response returned no roots");
  }

  // Validate at least one root has required fields
  bool hasValidRoot = false;
  for (std::size_t i = 0; i < roots.size(); ++i)
  {
    const auto &root = roots[i];
    if (!root.is_object())
    {
      continue;
    }

    if (root.contains("uri") && root.contains("name"))
    {
      hasValidRoot = true;
      break;
    }
  }

  if (!hasValidRoot)
  {
    throw std::runtime_error("roots/list response does not contain any valid root with uri and name");
  }

  std::cout << "cpp integration server roots/list assertions passed (received " << roots.size() << " root(s))" << '\n';
  std::cout.flush();

  // Mark roots list check as passed
  assertionsState.rootsListChangedReceived.store(true);
}

}  // namespace

auto main(int argc, char **argv) -> int
{
  try
  {
    const Options options = parseOptions(argc, argv);

    // Shared registry to track server instances created by the runner's factory
    auto serverRegistry = std::make_shared<ServerRegistry>();

    auto makeServer = [&options, &serverRegistry]() -> std::shared_ptr<mcp::Server>
    {
      mcp::ToolsCapability toolsCapability;
      mcp::ResourcesCapability resourcesCapability;
      mcp::PromptsCapability promptsCapability;

      mcp::ServerConfiguration configuration;
      configuration.capabilities = mcp::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, resourcesCapability, toolsCapability, std::nullopt, std::nullopt);
      configuration.serverInfo = mcp::Implementation("cpp-integration-server", "1.0.0");
      configuration.instructions = "Integration fixture server for reference SDK tests.";

      const std::shared_ptr<mcp::Server> server = mcp::Server::create(std::move(configuration));

      // Register this server in the registry
      {
        std::scoped_lock lock(serverRegistry->mutex);
        serverRegistry->servers.push_back(server);
      }

      // Tool to trigger roots change - signals to the Python test script that it should mutate roots
      mcp::ToolDefinition triggerRootsChangeTool;
      triggerRootsChangeTool.name = std::string(kTriggerRootsChangeToolName);
      triggerRootsChangeTool.description = "Signal to the test harness that roots should be changed";
      triggerRootsChangeTool.inputSchema = mcp::jsonrpc::JsonValue::object();
      triggerRootsChangeTool.inputSchema["type"] = "object";
      triggerRootsChangeTool.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();

      server->registerTool(std::move(triggerRootsChangeTool),
                           [](const mcp::ToolCallContext &context) -> mcp::CallToolResult
                           {
                             mcp::CallToolResult result;
                             result.content = mcp::jsonrpc::JsonValue::array();
                             mcp::jsonrpc::JsonValue textContent = mcp::jsonrpc::JsonValue::object();
                             textContent["type"] = "text";
                             textContent["text"] = "roots change triggered";
                             result.content.push_back(std::move(textContent));
                             return result;
                           });

      return server;
    };

    mcp::StreamableHttpServerRunnerOptions runnerOptions;
    runnerOptions.transportOptions.http.endpoint.bindAddress = options.bindAddress;
    runnerOptions.transportOptions.http.endpoint.bindLocalhostOnly = true;
    runnerOptions.transportOptions.http.endpoint.port = options.port;
    runnerOptions.transportOptions.http.endpoint.path = options.path;
    runnerOptions.transportOptions.http.requireSessionId = true;

    runnerOptions.transportOptions.http.authorization = mcp::auth::OAuthServerAuthorizationOptions {};
    runnerOptions.transportOptions.http.authorization->tokenVerifier = std::make_shared<StaticTokenVerifier>(options.bearerToken);
    runnerOptions.transportOptions.http.authorization->protectedResourceMetadata.resource = "https://cpp-integration.example/mcp";
    runnerOptions.transportOptions.http.authorization->protectedResourceMetadata.authorizationServers = {
      "https://auth.integration.example",
    };
    runnerOptions.transportOptions.http.authorization->protectedResourceMetadata.scopesSupported.values = {
      "mcp:read",
    };
    runnerOptions.transportOptions.http.authorization->defaultRequiredScopes.values = {
      "mcp:read",
    };

    mcp::StreamableHttpServerRunner runner(makeServer, std::move(runnerOptions));
    runner.start();

    std::cout << "cpp integration server listening on http://" << options.bindAddress << ":" << runner.localPort() << options.path << '\n';
    std::cout.flush();

    RootsAssertionsState rootsAssertions;

    // Set up a polling worker to wait for session state and run roots assertions
    // The worker polls for the first session to reach kOperating state (after notifications/initialized)
    rootsAssertions.worker = std::thread(
      [&runner, &serverRegistry, &rootsAssertions]() -> void
      {
        try
        {
          const auto startTime = std::chrono::steady_clock::now();
          std::shared_ptr<mcp::Server> targetServer;

          // Poll for a server that has reached kOperating state
          while (true)
          {
            const auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (elapsed > kSessionStateWaitTimeout)
            {
              throw std::runtime_error("timeout waiting for session to reach kOperating state");
            }

            if (!runner.isRunning())
            {
              throw std::runtime_error("runner stopped unexpectedly");
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
                  if (sessionState == mcp::SessionState::kOperating)
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

          // Register notification handler for roots/list_changed
          targetServer->registerNotificationHandler("notifications/roots/list_changed",
                                                    [&rootsAssertions](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Notification &) -> void
                                                    {
                                                      std::cout << "cpp integration server received notifications/roots/list_changed" << '\n';
                                                      std::cout.flush();
                                                      rootsAssertions.rootsListChangedReceived.store(true);
                                                    });

          // Run the roots assertions with an empty context
          // The runner's outbound message sender handles routing using stored session ID
          runRootsAssertions(*targetServer, mcp::jsonrpc::RequestContext {}, rootsAssertions);
          rootsAssertions.passed.store(true);
        }
        catch (const std::exception &error)
        {
          std::scoped_lock lock(rootsAssertions.mutex);
          rootsAssertions.failureReason = error.what();
        }
        catch (...)
        {
          std::scoped_lock lock(rootsAssertions.mutex);
          rootsAssertions.failureReason = "unknown roots assertion failure";
        }

        rootsAssertions.completed.store(true);
      });

    std::string ignoredLine;
    while (std::getline(std::cin, ignoredLine))
    {
    }

    if (rootsAssertions.worker.joinable())
    {
      rootsAssertions.worker.join();
    }

    runner.stop();

    // Check if roots assertions were started
    if (!rootsAssertions.completed.load())
    {
      // The worker might still be running or not started - this is a timeout case
      std::cerr << "cpp_server_roots_fixture failed: roots assertions did not complete" << '\n';
      return 3;
    }

    if (!rootsAssertions.passed.load())
    {
      std::optional<std::string> failureReason;
      {
        std::scoped_lock lock(rootsAssertions.mutex);
        failureReason = rootsAssertions.failureReason;
      }

      std::cerr << "cpp_server_roots_fixture failed: roots assertions failed";
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
    std::cerr << "cpp_server_roots_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
