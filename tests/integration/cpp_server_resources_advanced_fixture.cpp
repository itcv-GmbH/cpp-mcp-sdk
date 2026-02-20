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
  bool emitMarkers = false;
};

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
  std::vector<std::weak_ptr<mcp::Server>> servers;
  std::atomic<bool> outboundAssertionsTriggered {false};
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

    if (argument == "--emit-markers")
    {
      options.emitMarkers = true;
      continue;
    }

    throw std::invalid_argument("Unknown argument: " + std::string(argument));
  }

  return options;
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
      // Enable resource subscription and listChanged capabilities
      mcp::ResourcesCapability resourcesCapability;
      resourcesCapability.subscribe = true;
      resourcesCapability.listChanged = true;

      mcp::ServerConfiguration configuration;
      configuration.capabilities = mcp::ServerCapabilities(std::nullopt,  // logging
                                                           std::nullopt,  // completions
                                                           std::nullopt,  // prompts
                                                           resourcesCapability,  // resources
                                                           std::nullopt,  // tools
                                                           std::nullopt,  // tasks
                                                           std::nullopt  // experimental
      );
      configuration.serverInfo = mcp::Implementation("cpp-integration-server-resources-advanced", "1.0.0");
      configuration.instructions = "Advanced resources fixture server for reference SDK tests with templates and subscriptions.";

      const std::shared_ptr<mcp::Server> server = mcp::Server::create(std::move(configuration));

      // Register this server in the registry
      {
        std::scoped_lock lock(serverRegistry->mutex);
        serverRegistry->servers.push_back(server);
      }

      // Register a concrete resource
      mcp::ResourceDefinition infoResource;
      infoResource.uri = "resource://cpp-server-advanced/info";
      infoResource.name = "cpp-server-advanced-info";
      infoResource.description = "Reference data exposed by the C++ advanced resources integration fixture";
      infoResource.mimeType = "text/plain";

      server->registerResource(std::move(infoResource),
                               [](const mcp::ResourceReadContext &) -> std::vector<mcp::ResourceContent>
                               {
                                 return {
                                   mcp::ResourceContent::text("resource://cpp-server-advanced/info", "cpp integration advanced resource", std::string("text/plain")),
                                 };
                               });

      // Register a second concrete resource that will be used for subscription testing
      mcp::ResourceDefinition dynamicResource;
      dynamicResource.uri = "resource://cpp-server-advanced/dynamic";
      dynamicResource.name = "cpp-server-advanced-dynamic";
      dynamicResource.description = "Dynamic resource for subscription testing";
      dynamicResource.mimeType = "application/json";

      server->registerResource(std::move(dynamicResource),
                               [](const mcp::ResourceReadContext &) -> std::vector<mcp::ResourceContent>
                               {
                                 return {
                                   mcp::ResourceContent::text("resource://cpp-server-advanced/dynamic", R"({"value": "initial"})", std::string("application/json")),
                                 };
                               });

      // Register a resource template for resources/templates/list
      mcp::ResourceTemplateDefinition templateDefinition;
      templateDefinition.uriTemplate = "resource://cpp-server-advanced/item/{id}";
      templateDefinition.name = "cpp-server-advanced-item-template";
      templateDefinition.description = "Template for generating item resources";
      templateDefinition.mimeType = "application/json";

      server->registerResourceTemplate(std::move(templateDefinition));

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
    runnerOptions.transportOptions.http.authorization->protectedResourceMetadata.resource = "https://cpp-integration-advanced.example/mcp";
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

    std::cout << "cpp integration server resources advanced listening on http://" << options.bindAddress << ":" << runner.localPort() << options.path << '\n';
    std::cout.flush();

    // Emit marker for server started
    if (options.emitMarkers)
    {
      std::cout << "MARKER: SERVER_STARTED" << '\n';
      std::cout.flush();
    }

    OutboundAssertionsState outboundAssertions;

    // Set up a polling worker to wait for session state and emit resource-related markers
    outboundAssertions.worker = std::thread(
      [&runner, &serverRegistry, &outboundAssertions, &options]() -> void
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

          // Emit marker indicating server is ready
          if (options.emitMarkers)
          {
            std::cout << "MARKER: SERVER_READY" << '\n';
            std::cout.flush();
          }

          // Wait a bit then emit list changed notification to signal templates are registered
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
          targetServer->notifyResourcesListChanged();
          if (options.emitMarkers)
          {
            std::cout << "MARKER: RESOURCES_LIST_CHANGED_EMITTED" << '\n';
            std::cout.flush();
          }

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

    std::string ignoredLine;
    while (std::getline(std::cin, ignoredLine))
    {
    }

    if (outboundAssertions.worker.joinable())
    {
      outboundAssertions.worker.join();
    }

    runner.stop();

    // Check if outbound assertions were started
    if (!outboundAssertions.completed.load())
    {
      // The worker might still be running or not started - this is a timeout case
      std::cerr << "cpp_server_resources_advanced_fixture failed: assertions did not complete" << '\n';
      return 3;
    }

    if (!outboundAssertions.passed.load())
    {
      std::optional<std::string> failureReason;
      {
        std::scoped_lock lock(outboundAssertions.mutex);
        failureReason = outboundAssertions.failureReason;
      }

      std::cerr << "cpp_server_resources_advanced_fixture failed: assertions failed";
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
    std::cerr << "cpp_server_resources_advanced_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
