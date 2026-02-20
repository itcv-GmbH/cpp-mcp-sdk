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

#include <mcp/auth/oauth_server.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/tools.hpp>
#include <mcp/transport/http.hpp>

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

    mcp::ToolsCapability toolsCapability;
    toolsCapability.listChanged = true;

    mcp::PromptsCapability promptsCapability;
    promptsCapability.listChanged = true;

    mcp::ServerConfiguration configuration;
    configuration.capabilities = mcp::ServerCapabilities(std::nullopt, std::nullopt, promptsCapability, std::nullopt, toolsCapability, std::nullopt, std::nullopt);
    configuration.serverInfo = mcp::Implementation("example-http-auth-server", "1.0.0");
    configuration.instructions = "Send a bearer token to call tools.";

    const std::shared_ptr<mcp::Server> server = mcp::Server::create(std::move(configuration));

    mcp::ToolDefinition whoAmITool;
    whoAmITool.name = "who_am_i";
    whoAmITool.description = "Return authorization context from bearer token";
    whoAmITool.inputSchema = mcp::jsonrpc::JsonValue::object();
    whoAmITool.inputSchema["type"] = "object";
    whoAmITool.inputSchema["properties"] = mcp::jsonrpc::JsonValue::object();

    server->registerTool(std::move(whoAmITool),
                         [](const mcp::ToolCallContext &context) -> mcp::CallToolResult
                         {
                           mcp::CallToolResult result;
                           result.content = mcp::jsonrpc::JsonValue::array();

                           mcp::jsonrpc::JsonValue content = mcp::jsonrpc::JsonValue::object();
                           content["type"] = "text";
                           content["text"] = "authorized as auth context: " + context.requestContext.authContext.value_or("<none>");
                           result.content.push_back(std::move(content));

                           result.structuredContent = mcp::jsonrpc::JsonValue::object();
                           (*result.structuredContent)["authContext"] = context.requestContext.authContext.value_or("");
                           return result;
                         });

    mcp::transport::http::StreamableHttpServerOptions streamableOptions;
    streamableOptions.http.endpoint.bindAddress = options.bindAddress;
    streamableOptions.http.endpoint.bindLocalhostOnly = false;
    streamableOptions.http.endpoint.port = options.port;
    streamableOptions.http.endpoint.path = options.path;

    streamableOptions.http.authorization = mcp::auth::OAuthServerAuthorizationOptions {};
    streamableOptions.http.authorization->tokenVerifier = std::make_shared<ExampleTokenVerifier>();
    streamableOptions.http.authorization->protectedResourceMetadata.resource =
      (options.tlsCert.has_value() ? "https://" : "http://") + options.bindAddress + ":" + std::to_string(options.port) + options.path;
    streamableOptions.http.authorization->protectedResourceMetadata.authorizationServers = {
      "https://auth.example.test",
    };
    streamableOptions.http.authorization->protectedResourceMetadata.scopesSupported.values = {
      "mcp:read",
      "mcp:write",
    };
    streamableOptions.http.authorization->defaultRequiredScopes.values = {
      "mcp:read",
    };

    if (options.tlsCert.has_value() && options.tlsKey.has_value())
    {
      mcp_http::ServerTlsConfiguration tls;
      tls.certificateChainFile = *options.tlsCert;
      tls.privateKeyFile = *options.tlsKey;
      streamableOptions.http.tls = std::move(tls);
    }

    mcp_http::StreamableHttpServer streamableServer(streamableOptions);

    streamableServer.setRequestHandler(
      [&server](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Request &request) -> mcp_http::StreamableRequestResult
      {
        mcp_http::StreamableRequestResult result;
        result.response = server->handleRequest(context, request).get();
        return result;
      });

    streamableServer.setNotificationHandler(
      [&server](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Notification &notification) -> bool
      {
        server->handleNotification(context, notification);
        return true;
      });

    streamableServer.setResponseHandler([&server](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Response &response) -> bool
                                        { return server->handleResponse(context, response); });

    server->setOutboundMessageSender(
      [&streamableServer](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Message &message) -> void
      {
        if (!streamableServer.enqueueServerMessage(message, context.sessionId))
        {
          std::cerr << "failed to enqueue server-initiated message for active stream" << '\n';
        }
      });

    mcp::transport::HttpServerOptions runtimeOptions = streamableOptions.http;
    mcp::transport::HttpServerRuntime runtime(std::move(runtimeOptions));
    runtime.setRequestHandler([&streamableServer](const mcp_http::ServerRequest &request) -> mcp_http::ServerResponse { return streamableServer.handleRequest(request); });
    runtime.start();

    const std::string scheme = options.tlsCert.has_value() ? "https" : "http";
    const std::string endpoint = scheme + "://" + options.bindAddress + ":" + std::to_string(runtime.localPort()) + options.path;

    std::cout << "http_server_auth running at " << endpoint << '\n';
    std::cout << "Try bearer tokens: dev-token-read, dev-token-write" << '\n';
    std::cout << "Press ENTER to stop." << '\n';
    std::cout.flush();

    std::string ignore;
    static_cast<void>(std::getline(std::cin, ignore));

    runtime.stop();
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "http_server_auth failed: " << error.what() << '\n';
    return 1;
  }
}
