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

#include <mcp/auth/all.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server.hpp>
#include <mcp/server/all.hpp>
#include <mcp/server/streamable_http_runner.hpp>
#include <mcp/transport/all.hpp>

namespace
{

namespace mcp_http = mcp::transport::http;

class ExampleTokenVerifier final : public mcp::auth::OAuthTokenVerifier
{
public:
  auto verifyToken(const mcp::auth::OAuthTokenVerificationRequest &request) const -> mcp::auth::OAuthTokenVerificationResult override
  {
    mcp::auth::OAuthTokenVerificationResult result;

    if (request.bearerToken == "dev-token-read")
    {
      result.status =
        hasAllScopes({"mcp:read"}, request.requiredScopes.values) ? mcp::auth::OAuthTokenVerificationStatus::kValid : mcp::auth::OAuthTokenVerificationStatus::kInsufficientScope;
      result.authorizationContext.taskIsolationKey = "dev-token-read";
      result.authorizationContext.subject = "example-user";
      result.authorizationContext.grantedScopes.values = {"mcp:read"};
      result.audienceBound = true;
      return result;
    }

    if (request.bearerToken == "dev-token-write")
    {
      result.status = hasAllScopes({"mcp:read", "mcp:write"}, request.requiredScopes.values) ? mcp::auth::OAuthTokenVerificationStatus::kValid
                                                                                             : mcp::auth::OAuthTokenVerificationStatus::kInsufficientScope;
      result.authorizationContext.taskIsolationKey = "dev-token-write";
      result.authorizationContext.subject = "example-user";
      result.authorizationContext.grantedScopes.values = {"mcp:read", "mcp:write"};
      result.audienceBound = true;
      return result;
    }

    return result;
  }

private:
  static auto hasAllScopes(const std::vector<std::string> &available, const std::vector<std::string> &required) -> bool
  {
    for (const std::string &requiredScope : required)
    {
      bool found = false;
      for (const std::string &availableScope : available)
      {
        if (availableScope == requiredScope)
        {
          found = true;
          break;
        }
      }

      if (!found)
      {
        return false;
      }
    }

    return true;
  }
};

struct Options
{
  std::string bindAddress = "127.0.0.1";
  std::uint16_t port = 8443;
  std::string path = "/mcp";
  std::optional<std::string> tlsCert;
  std::optional<std::string> tlsKey;
};

constexpr std::uint64_t kMaxPort = std::numeric_limits<std::uint16_t>::max();

auto parsePort(const std::string &value) -> std::uint16_t  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  const auto parsed = static_cast<std::uint64_t>(std::strtoull(value.c_str(), nullptr, 10));  // NOLINT(cert-err34-c)
  if (parsed > kMaxPort)
  {
    throw std::invalid_argument("port must be <= 65535");
  }

  return static_cast<std::uint16_t>(parsed);
}

auto parseOptions(const std::vector<std::string> &arguments) -> Options  // NOLINT(llvm-prefer-static-over-anonymous-namespace)
{
  Options options;

  for (std::size_t index = 0; index < arguments.size(); ++index)
  {
    const std::string_view argument = arguments[index];
    const auto requireValue = [&arguments, index](std::string_view name) -> std::string
    {
      if (index + 1 >= arguments.size())
      {
        throw std::invalid_argument("Missing value for argument: " + std::string(name));
      }

      return arguments[index + 1];
    };

    if (argument == "--bind")
    {
      options.bindAddress = requireValue(argument);
      ++index;
      continue;
    }

    if (argument == "--port")
    {
      options.port = parsePort(requireValue(argument));
      ++index;
      continue;
    }

    if (argument == "--path")
    {
      options.path = requireValue(argument);
      ++index;
      continue;
    }

    if (argument == "--tls-cert")
    {
      options.tlsCert = requireValue(argument);
      ++index;
      continue;
    }

    if (argument == "--tls-key")
    {
      options.tlsKey = requireValue(argument);
      ++index;
      continue;
    }

    throw std::invalid_argument("Unknown argument: " + std::string(argument));
  }

  if (options.tlsCert.has_value() != options.tlsKey.has_value())
  {
    throw std::invalid_argument("--tls-cert and --tls-key must be used together");
  }

  if (!options.tlsCert.has_value())
  {
    throw std::invalid_argument("This example requires HTTPS. Provide --tls-cert and --tls-key.");
  }

  return options;
}

}  // namespace

auto createServer() -> std::shared_ptr<mcp::server::Server>
{
  mcp::lifecycle::session::ToolsCapability toolsCapability;
  toolsCapability.listChanged = true;

  mcp::lifecycle::session::PromptsCapability promptsCapability;
  promptsCapability.listChanged = true;

  mcp::server::ServerConfiguration configuration;
  configuration.capabilities =
    mcp::lifecycle::session::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, std::nullopt, toolsCapability, std::nullopt, std::nullopt);
  configuration.serverInfo = mcp::lifecycle::session::Implementation("example-http-auth-server", "1.0.0");
  configuration.instructions = "Send a bearer token to call tools.";

  const std::shared_ptr<mcp::server::Server> server = mcp::server::Server::create(std::move(configuration));

  mcp::server::ToolDefinition whoAmITool;
  whoAmITool.name = "who_am_i";
  whoAmITool.description = "Return authorization context from bearer token";
  whoAmITool.inputSchema = mcp::jsonrpc::JsonValue::object();
  whoAmITool.inputSchema["type"] = "object";
  whoAmITool.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();

  server->registerTool(std::move(whoAmITool),
                       [](const mcp::server::ToolCallContext &context) -> mcp::server::CallToolResult
                       {
                         mcp::server::CallToolResult result;
                         result.content = mcp::jsonrpc::JsonValue::array();

                         mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
                         content["type"] = "text";
                         content["text"] = "authorized as auth context: " + context.requestContext.authContext.value_or("<none>");
                         result.content.push_back(std::move(content));

                         result.structuredContent = mcp::jsonrpc::JsonValue::object();
                         (*result.structuredContent)["authContext"] = context.requestContext.authContext.value_or("");
                         return result;
                       });

  return server;
}

auto main(int argc, char *argv[]) -> int
{
  try
  {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int index = 1; index < argc; ++index)
    {
      arguments.emplace_back(argv[index]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }

    const Options options = parseOptions(arguments);

    mcp::server::StreamableHttpServerRunnerOptions runnerOptions;

    runnerOptions.transportOptions.http.endpoint.bindAddress = options.bindAddress;
    runnerOptions.transportOptions.http.endpoint.bindLocalhostOnly = false;
    runnerOptions.transportOptions.http.endpoint.port = options.port;
    runnerOptions.transportOptions.http.endpoint.path = options.path;

    // Enable session ID requirement for multi-client safety
    runnerOptions.transportOptions.http.requireSessionId = true;

    runnerOptions.transportOptions.http.authorization = mcp::auth::OAuthServerAuthorizationOptions {};
    runnerOptions.transportOptions.http.authorization->tokenVerifier = std::make_shared<ExampleTokenVerifier>();
    runnerOptions.transportOptions.http.authorization->protectedResourceMetadata.resource =
      (options.tlsCert.has_value() ? "https://" : "http://") + options.bindAddress + ":" + std::to_string(options.port) + options.path;
    runnerOptions.transportOptions.http.authorization->protectedResourceMetadata.authorizationServers = {
      "https://auth.example.test",
    };
    runnerOptions.transportOptions.http.authorization->protectedResourceMetadata.scopesSupported.values = {
      "mcp:read",
      "mcp:write",
    };
    runnerOptions.transportOptions.http.authorization->defaultRequiredScopes.values = {
      "mcp:read",
    };

    if (options.tlsCert.has_value() && options.tlsKey.has_value())
    {
      mcp_http::ServerTlsConfiguration tls;
      tls.certificateChainFile = *options.tlsCert;
      tls.privateKeyFile = *options.tlsKey;
      runnerOptions.transportOptions.http.tls = std::move(tls);
    }

    mcp::server::StreamableHttpServerRunner runner(createServer, runnerOptions);
    runner.start();

    const std::string scheme = options.tlsCert.has_value() ? "https" : "http";
    const std::string endpoint = scheme + "://" + options.bindAddress + ":" + std::to_string(runner.localPort()) + options.path;

    std::cout << "http_server_auth running at " << endpoint << '\n';
    std::cout << "Try bearer tokens: dev-token-read, dev-token-write" << '\n';
    std::cout << "Press ENTER to stop." << '\n';
    std::cout.flush();

    std::string ignore;
    static_cast<void>(std::getline(std::cin, ignore));

    runner.stop();
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "http_server_auth failed: " << error.what() << '\n';
    return 1;
  }
}
