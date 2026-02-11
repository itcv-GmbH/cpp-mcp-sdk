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

#include <mcp/auth/oauth_server.hpp>
#include <mcp/jsonrpc/messages.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/server/prompts.hpp>
#include <mcp/server/resources.hpp>
#include <mcp/server/server.hpp>
#include <mcp/server/tools.hpp>
#include <mcp/transport/http.hpp>

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
constexpr std::int64_t kSamplingRequestId = 4101;
constexpr std::int64_t kElicitationRequestId = 4102;
constexpr std::string_view kExpectedSamplingResponseText = "reference-client-sampling-response";
constexpr std::string_view kExpectedElicitationReason = "reference-client-confirmed";
constexpr std::string_view kIntegrationSessionId = "cpp-integration-session";

struct OutboundAssertionsState
{
  std::atomic<bool> started {false};
  std::atomic<bool> completed {false};
  std::atomic<bool> passed {false};
  std::mutex mutex;
  std::optional<std::string> failureReason;
  std::thread worker;
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

auto runOutboundAssertions(mcp::Server &server, const mcp::jsonrpc::RequestContext &context) -> void
{
  mcp::jsonrpc::Request samplingRequest;
  samplingRequest.id = std::int64_t {kSamplingRequestId};
  samplingRequest.method = "sampling/createMessage";
  samplingRequest.params = mcp::jsonrpc::JsonValue::object();
  (*samplingRequest.params)["maxTokens"] = std::int64_t {64};
  (*samplingRequest.params)["messages"] = mcp::jsonrpc::JsonValue::array();

  mcp::jsonrpc::JsonValue samplingMessage = mcp::jsonrpc::JsonValue::object();
  samplingMessage["role"] = "user";
  samplingMessage["content"] = mcp::jsonrpc::JsonValue::object();
  samplingMessage["content"]["type"] = "text";
  samplingMessage["content"]["text"] = "cpp-server-sampling-check";
  (*samplingRequest.params)["messages"].push_back(std::move(samplingMessage));

  std::future<mcp::jsonrpc::Response> samplingFuture = server.sendRequest(context, std::move(samplingRequest));
  const mcp::jsonrpc::Response samplingResponse = awaitResponse(samplingFuture, "sampling/createMessage");
  throwIfErrorResponse(samplingResponse, "sampling/createMessage");
  if (!std::holds_alternative<mcp::jsonrpc::SuccessResponse>(samplingResponse))
  {
    throw std::runtime_error("sampling/createMessage did not return a success response");
  }

  const auto &samplingResult = std::get<mcp::jsonrpc::SuccessResponse>(samplingResponse).result;
  if (!samplingResult.is_object() || !samplingResult.contains("content") || !samplingResult["content"].is_object() || !samplingResult["content"].contains("text")
      || !samplingResult["content"]["text"].is_string())
  {
    throw std::runtime_error("sampling/createMessage response did not include content.text");
  }

  const std::string samplingText = samplingResult["content"]["text"].as<std::string>();
  if (samplingText != kExpectedSamplingResponseText)
  {
    throw std::runtime_error("sampling/createMessage returned unexpected text: " + samplingText);
  }

  mcp::jsonrpc::Request elicitationRequest;
  elicitationRequest.id = std::int64_t {kElicitationRequestId};
  elicitationRequest.method = "elicitation/create";
  elicitationRequest.params = mcp::jsonrpc::JsonValue::object();
  (*elicitationRequest.params)["mode"] = "form";
  (*elicitationRequest.params)["message"] = "cpp-server-elicitation-check";
  (*elicitationRequest.params)["requestedSchema"] = mcp::jsonrpc::JsonValue::object();
  (*elicitationRequest.params)["requestedSchema"]["type"] = "object";
  (*elicitationRequest.params)["requestedSchema"]["properties"] = mcp::jsonrpc::JsonValue::object();
  (*elicitationRequest.params)["requestedSchema"]["properties"]["approved"] = mcp::jsonrpc::JsonValue::object();
  (*elicitationRequest.params)["requestedSchema"]["properties"]["approved"]["type"] = "boolean";
  (*elicitationRequest.params)["requestedSchema"]["properties"]["reason"] = mcp::jsonrpc::JsonValue::object();
  (*elicitationRequest.params)["requestedSchema"]["properties"]["reason"]["type"] = "string";
  (*elicitationRequest.params)["requestedSchema"]["required"] = mcp::jsonrpc::JsonValue::array();
  (*elicitationRequest.params)["requestedSchema"]["required"].push_back("approved");
  (*elicitationRequest.params)["requestedSchema"]["required"].push_back("reason");

  std::future<mcp::jsonrpc::Response> elicitationFuture = server.sendRequest(context, std::move(elicitationRequest));
  const mcp::jsonrpc::Response elicitationResponse = awaitResponse(elicitationFuture, "elicitation/create");
  throwIfErrorResponse(elicitationResponse, "elicitation/create");
  if (!std::holds_alternative<mcp::jsonrpc::SuccessResponse>(elicitationResponse))
  {
    throw std::runtime_error("elicitation/create did not return a success response");
  }

  const auto &elicitationResult = std::get<mcp::jsonrpc::SuccessResponse>(elicitationResponse).result;
  if (!elicitationResult.is_object() || !elicitationResult.contains("action") || !elicitationResult["action"].is_string())
  {
    throw std::runtime_error("elicitation/create response did not include action");
  }

  const std::string action = elicitationResult["action"].as<std::string>();
  if (action != "accept")
  {
    throw std::runtime_error("elicitation/create returned unexpected action: " + action);
  }

  if (!elicitationResult.contains("content") || !elicitationResult["content"].is_object())
  {
    throw std::runtime_error("elicitation/create response did not include content object");
  }

  const auto &content = elicitationResult["content"];
  if (!content.contains("approved") || !content["approved"].is_bool() || !content["approved"].as<bool>())
  {
    throw std::runtime_error("elicitation/create content.approved was not true");
  }

  if (!content.contains("reason") || !content["reason"].is_string())
  {
    throw std::runtime_error("elicitation/create content.reason was missing");
  }

  const std::string reason = content["reason"].as<std::string>();
  if (reason != kExpectedElicitationReason)
  {
    throw std::runtime_error("elicitation/create content.reason was unexpected: " + reason);
  }

  std::cout << "cpp integration server outbound sampling/elicitation assertions passed" << '\n';
  std::cout.flush();
}

auto isInitializeRequest(const mcp::transport::http::ServerRequest &request) -> bool
{
  if (request.method != mcp::transport::http::ServerRequestMethod::kPost)
  {
    return false;
  }

  try
  {
    const mcp::jsonrpc::Message message = mcp::jsonrpc::parseMessage(request.body);
    if (!std::holds_alternative<mcp::jsonrpc::Request>(message))
    {
      return false;
    }

    return std::get<mcp::jsonrpc::Request>(message).method == "initialize";
  }
  catch (...)
  {
    return false;
  }
}

}  // namespace

auto main(int argc, char **argv) -> int
{
  try
  {
    const Options options = parseOptions(argc, argv);

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

    mcp::transport::http::StreamableHttpServerOptions streamableOptions;
    streamableOptions.http.endpoint.bindAddress = options.bindAddress;
    streamableOptions.http.endpoint.bindLocalhostOnly = true;
    streamableOptions.http.endpoint.port = options.port;
    streamableOptions.http.endpoint.path = options.path;

    streamableOptions.http.authorization = mcp::auth::OAuthServerAuthorizationOptions {};
    streamableOptions.http.authorization->tokenVerifier = std::make_shared<StaticTokenVerifier>(options.bearerToken);
    streamableOptions.http.authorization->protectedResourceMetadata.resource = "https://cpp-integration.example/mcp";
    streamableOptions.http.authorization->protectedResourceMetadata.authorizationServers = {
      "https://auth.integration.example",
    };
    streamableOptions.http.authorization->protectedResourceMetadata.scopesSupported.values = {
      "mcp:read",
    };
    streamableOptions.http.authorization->defaultRequiredScopes.values = {
      "mcp:read",
    };

    mcp::transport::http::StreamableHttpServer streamableServer(streamableOptions);
    OutboundAssertionsState outboundAssertions;

    streamableServer.setRequestHandler(
      [&server](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Request &request) -> mcp::transport::http::StreamableRequestResult
      {
        mcp::transport::http::StreamableRequestResult result;
        result.response = server->handleRequest(context, request).get();
        return result;
      });

    streamableServer.setNotificationHandler(
      [&server, &outboundAssertions](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Notification &notification) -> bool
      {
        server->handleNotification(context, notification);

        if (notification.method == "notifications/initialized")
        {
          bool expected = false;
          if (outboundAssertions.started.compare_exchange_strong(expected, true))
          {
            const mcp::jsonrpc::RequestContext requestContext = context;
            outboundAssertions.worker = std::thread(
              [server, &outboundAssertions, requestContext]() -> void
              {
                try
                {
                  runOutboundAssertions(*server, requestContext);
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
          }
        }

        return true;
      });

    streamableServer.setResponseHandler([&server](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Response &response) -> bool
                                        { return server->handleResponse(context, response); });

    server->setOutboundMessageSender([&streamableServer](const mcp::jsonrpc::RequestContext &context, const mcp::jsonrpc::Message &message) -> void
                                     { static_cast<void>(streamableServer.enqueueServerMessage(message, context.sessionId)); });

    mcp::transport::HttpServerOptions runtimeOptions = streamableOptions.http;
    mcp::transport::HttpServerRuntime runtime(std::move(runtimeOptions));
    runtime.setRequestHandler(
      [&streamableServer](const mcp::transport::http::ServerRequest &request) -> mcp::transport::http::ServerResponse
      {
        mcp::transport::http::ServerResponse response = streamableServer.handleRequest(request);
        if (response.statusCode == 200 && isInitializeRequest(request))
        {
          mcp::transport::http::setHeader(response.headers, mcp::transport::http::kHeaderMcpSessionId, std::string(kIntegrationSessionId));
          streamableServer.upsertSession(std::string(kIntegrationSessionId));
        }

        return response;
      });
    runtime.start();

    std::cout << "cpp integration server listening on http://" << options.bindAddress << ":" << runtime.localPort() << options.path << '\n';
    std::cout.flush();

    std::string ignoredLine;
    while (std::getline(std::cin, ignoredLine))
    {
    }

    if (outboundAssertions.worker.joinable())
    {
      outboundAssertions.worker.join();
    }

    if (!outboundAssertions.started.load())
    {
      std::cerr << "cpp_server_fixture failed: outbound sampling/elicitation assertions never started" << '\n';
      runtime.stop();
      return 2;
    }

    if (!outboundAssertions.completed.load() || !outboundAssertions.passed.load())
    {
      std::optional<std::string> failureReason;
      {
        std::scoped_lock lock(outboundAssertions.mutex);
        failureReason = outboundAssertions.failureReason;
      }

      std::cerr << "cpp_server_fixture failed: outbound sampling/elicitation assertions failed";
      if (failureReason.has_value())
      {
        std::cerr << " (" << *failureReason << ')';
      }
      std::cerr << '\n';

      runtime.stop();
      return 3;
    }

    runtime.stop();
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "cpp_server_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
