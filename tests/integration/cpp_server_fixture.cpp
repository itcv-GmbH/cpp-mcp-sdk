#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

auto makeTextContent(std::string text) -> mcp::jsonrpc::JsonValue
{
  mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
  content["type"] = "text";
  content["text"] = std::move(text);
  return content;
}

}  // namespace

auto main(int argc, char **argv) -> int
{
  try
  {
    const Options options = parseOptions(argc, argv);

    auto makeServer = [&options]() -> std::shared_ptr<mcp::Server>
    {
      mcp::ToolsCapability toolsCapability;
      mcp::ResourcesCapability resourcesCapability;
      mcp::PromptsCapability promptsCapability;

      mcp::ServerConfiguration configuration;
      configuration.capabilities = mcp::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, resourcesCapability, toolsCapability, std::nullopt, std::nullopt);
      configuration.serverInfo = mcp::Implementation("cpp-integration-server", "1.0.0");
      configuration.instructions = "Integration fixture server for reference SDK tests.";

      const std::shared_ptr<mcp::Server> server = mcp::Server::create(std::move(configuration));

      mcp::ToolDefinition echoTool;
      echoTool.name = "cpp_echo";
      echoTool.description = "Echo text from arguments.text";
      echoTool.inputSchema = mcp::jsonrpc::JsonValue::object();
      echoTool.inputSchema["type"] = "object";
      echoTool.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();
      echoTool.inputSchema["properties"]["text"] = mcp::jsonrpc::JsonValue::object();
      echoTool.inputSchema["properties"]["text"]["type"] = "string";
      echoTool.inputSchema["required"] = mcp::jsonrpc::JsonValue::array();
      echoTool.inputSchema["required"].push_back("text");

      server->registerTool(std::move(echoTool),
                           [](const mcp::ToolCallContext &context) -> mcp::CallToolResult
                           {
                             mcp::CallToolResult result;
                             result.content = mcp::jsonrpc::JsonValue::array();
                             result.content.push_back(makeTextContent("cpp echo: " + context.arguments["text"].as<std::string>()));
                             return result;
                           });

      mcp::ResourceDefinition infoResource;
      infoResource.uri = "resource://cpp-server/info";
      infoResource.name = "cpp-server-info";
      infoResource.description = "Reference data exposed by the C++ integration fixture";
      infoResource.mimeType = "text/plain";

      server->registerResource(std::move(infoResource),
                               [](const mcp::ResourceReadContext &) -> std::vector<mcp::ResourceContent>
                               {
                                 return {
                                   mcp::ResourceContent::text("resource://cpp-server/info", "cpp integration resource", std::string("text/plain")),
                                 };
                               });

      mcp::PromptDefinition prompt;
      prompt.name = "cpp_server_prompt";
      prompt.description = "Returns a prompt with the provided topic";

      mcp::PromptArgumentDefinition topicArgument;
      topicArgument.name = "topic";
      topicArgument.required = true;
      prompt.arguments.push_back(std::move(topicArgument));

      server->registerPrompt(std::move(prompt),
                             [](const mcp::PromptGetContext &context) -> mcp::PromptGetResult
                             {
                               mcp::PromptGetResult result;
                               result.description = "C++ integration prompt";

                               mcp::PromptMessage message;
                               message.role = "user";
                               message.content = mcp::jsonrpc::JsonValue::object();
                               message.content["type"] = "text";
                               message.content["text"] = "C++ prompt topic: " + context.arguments["topic"].as<std::string>();
                               result.messages.push_back(std::move(message));
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

    std::string ignoredLine;
    while (std::getline(std::cin, ignoredLine))
    {
    }

    runner.stop();
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "cpp_server_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
