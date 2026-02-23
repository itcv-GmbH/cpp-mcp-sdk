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

}  // namespace

auto main(int argc, char **argv) -> int
{
  try
  {
    const Options options = parseOptions(argc, argv);

    auto makeServer = [&options]() -> std::shared_ptr<mcp::server::Server>
    {
      // Register utilities capability (logging and completions)
      mcp::lifecycle::session::LoggingCapability loggingCapability;
      mcp::lifecycle::session::CompletionsCapability completionsCapability;
      mcp::lifecycle::session::PromptsCapability promptsCapability;
      mcp::lifecycle::session::ResourcesCapability resourcesCapability;
      mcp::lifecycle::session::ToolsCapability toolsCapability;

      mcp::ServerConfiguration configuration;
      configuration.capabilities =
        mcp::lifecycle::session::ServerCapabilities(loggingCapability, completionsCapability, promptsCapability, resourcesCapability, toolsCapability, std::nullopt, std::nullopt);
      configuration.serverInfo = mcp::lifecycle::session::Implementation("cpp-integration-utilities-server", "1.0.0");
      configuration.instructions = "Integration fixture server for reference SDK utility tests.";

      const std::shared_ptr<mcp::server::Server> server = mcp::server::Server::create(std::move(configuration));

      // Register ping handler - returns empty result
      server->registerRequestHandler("ping",
                                     [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                     {
                                       return std::async(std::launch::deferred,
                                                         [&request]() -> mcp::jsonrpc::Response
                                                         {
                                                           mcp::jsonrpc::SuccessResponse response;
                                                           response.id = request.id;
                                                           response.result = mcp::jsonrpc::JsonValue::object();
                                                           return response;
                                                         });
                                     });

      // Register logging/setLevel handler
      server->registerRequestHandler("logging/setLevel",
                                     [&server](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                     {
                                       return std::async(std::launch::deferred,
                                                         [&server, &context, &request]() -> mcp::jsonrpc::Response
                                                         {
                                                           // Set log level based on params if provided
                                                           if (request.params && request.params->is_object() && request.params->contains("level"))
                                                           {
                                                             const std::string level = (*request.params)["level"].as<std::string>();
                                                             // Map string level to LogLevel enum
                                                             if (level == "debug")
                                                             {
                                                               server->emitLogMessage(context, mcp::LogLevel::kDebug, "Log level set to debug");
                                                             }
                                                             else if (level == "info")
                                                             {
                                                               server->emitLogMessage(context, mcp::LogLevel::kInfo, "Log level set to info");
                                                             }
                                                             else if (level == "warning" || level == "warn")
                                                             {
                                                               server->emitLogMessage(context, mcp::LogLevel::kWarning, "Log level set to warning");
                                                             }
                                                             else if (level == "error")
                                                             {
                                                               server->emitLogMessage(context, mcp::LogLevel::kError, "Log level set to error");
                                                             }
                                                           }

                                                           // Send notifications/message to confirm log level was set
                                                           mcp::jsonrpc::Notification logNotification;
                                                           logNotification.method = "notifications/message";
                                                           logNotification.params = mcp::jsonrpc::JsonValue::object();
                                                           (*logNotification.params)["level"] = "info";
                                                           (*logNotification.params)["data"] = "Log level set";
                                                           server->sendNotification(context, std::move(logNotification));

                                                           mcp::jsonrpc::SuccessResponse response;
                                                           response.id = request.id;
                                                           response.result = mcp::jsonrpc::JsonValue::object();
                                                           return response;
                                                         });
                                     });

      // Register completion/complete handler
      server->registerRequestHandler("completion/complete",
                                     [](const mcp::jsonrpc::RequestContext &, const mcp::jsonrpc::Request &request) -> std::future<mcp::jsonrpc::Response>
                                     {
                                       return std::async(std::launch::deferred,
                                                         [&request]() -> mcp::jsonrpc::Response
                                                         {
                                                           mcp::jsonrpc::JsonValue result = mcp::jsonrpc::JsonValue::object();
                                                           result["completion"] = mcp::jsonrpc::JsonValue::object();
                                                           result["completion"]["items"] = mcp::jsonrpc::JsonValue::array();

                                                           // Return completion items for prompts and resources
                                                           mcp::jsonrpc::JsonValue promptItem = mcp::jsonrpc::JsonValue::object();
                                                           promptItem["label"] = "cpp_server_prompt";
                                                           promptItem["detail"] = "C++ integration prompt";
                                                           result["completion"]["items"].push_back(std::move(promptItem));

                                                           mcp::jsonrpc::JsonValue resourceItem = mcp::jsonrpc::JsonValue::object();
                                                           resourceItem["label"] = "cpp-server://info";
                                                           resourceItem["detail"] = "Reference data exposed by the C++ integration fixture";
                                                           result["completion"]["items"].push_back(std::move(resourceItem));

                                                           mcp::jsonrpc::SuccessResponse response;
                                                           response.id = request.id;
                                                           response.result = std::move(result);
                                                           return response;
                                                         });
                                     });

      return server;
    };

    mcp::server::StreamableHttpServerRunnerOptions runnerOptions;
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

    mcp::server::StreamableHttpServerRunner runner(makeServer, std::move(runnerOptions));
    runner.start();

    std::cout << "cpp integration server listening on http://" << options.bindAddress << ":" << runner.localPort() << options.path << '\n';
    std::cout.flush();

    // Wait for stdin to be closed
    std::string ignoredLine;
    while (std::getline(std::cin, ignoredLine))
    {
    }

    runner.stop();

    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "cpp_server_utilities_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
