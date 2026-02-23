#include <algorithm>
#include <cctype>
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

#include <mcp/client/client.hpp>
#include <mcp/jsonrpc/all.hpp>
#include <mcp/lifecycle/session.hpp>
#include <mcp/transport/all.hpp>

namespace
{

struct Options
{
  std::string endpoint;
  std::optional<std::string> token;
  bool expectUnauthorized = false;
};

constexpr std::string_view kExpectedServerSamplingPrompt = "python-server-sampling-check";
constexpr std::string_view kExpectedClientSamplingResponse = "cpp-client-sampling-response";
constexpr std::string_view kExpectedServerElicitationMessage = "python-server-elicitation-check";
constexpr std::string_view kExpectedClientElicitationReason = "cpp-client-approved";

auto toLowerAscii(std::string value) -> std::string
{
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) -> char { return static_cast<char>(std::tolower(character)); });
  return value;
}

auto containsAuthorizationSignal(std::string text) -> bool
{
  const std::string lower = toLowerAscii(std::move(text));
  return lower.find("401") != std::string::npos || lower.find("unauthorized") != std::string::npos || lower.find("authorization") != std::string::npos;
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

    if (argument == "--expect-unauthorized")
    {
      options.expectUnauthorized = true;
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

auto hasTool(const std::vector<mcp::ToolDefinition> &tools, std::string_view name) -> bool
{
  for (const mcp::ToolDefinition &tool : tools)
  {
    if (tool.name == name)
    {
      return true;
    }
  }

  return false;
}

auto hasResource(const std::vector<mcp::ResourceDefinition> &resources, std::string_view uri) -> bool
{
  for (const mcp::ResourceDefinition &resource : resources)
  {
    if (resource.uri == uri)
    {
      return true;
    }
  }

  return false;
}

auto hasPrompt(const std::vector<mcp::PromptDefinition> &prompts, std::string_view name) -> bool
{
  for (const mcp::PromptDefinition &prompt : prompts)
  {
    if (prompt.name == name)
    {
      return true;
    }
  }

  return false;
}

auto firstTextContent(const mcp::CallToolResult &result) -> std::string
{
  if (!result.content.is_array() || result.content.empty())
  {
    return {};
  }

  const mcp::jsonrpc::JsonValue &entry = result.content[0];
  if (!entry.is_object() || !entry.contains("text") || !entry["text"].is_string())
  {
    return {};
  }

  return entry["text"].as<std::string>();
}

auto promptContainsTopic(const mcp::PromptGetResult &promptResult, std::string_view topic) -> bool
{
  for (const mcp::PromptMessage &message : promptResult.messages)
  {
    if (!message.content.is_object() || !message.content.contains("text") || !message.content["text"].is_string())
    {
      continue;
    }

    if (message.content["text"].as<std::string>().find(topic) != std::string::npos)
    {
      return true;
    }
  }

  return false;
}

auto initializeSucceeded(const mcp::jsonrpc::Response &response) -> bool
{
  return std::holds_alternative<mcp::jsonrpc::SuccessResponse>(response);
}

auto verifyUnauthorizedHttpStatus(std::string_view endpoint) -> void
{
  mcp::transport::http::HttpClientOptions httpOptions;
  httpOptions.endpointUrl = std::string(endpoint);
  mcp::transport::http::HttpClientRuntime httpRuntime(std::move(httpOptions));

  mcp::jsonrpc::Request initializeRequest;
  initializeRequest.id = std::int64_t {1};
  initializeRequest.method = "initialize";
  initializeRequest.params = mcp::jsonrpc::JsonValue::object();
  (*initializeRequest.params)["protocolVersion"] = "2025-11-25";
  (*initializeRequest.params)["capabilities"] = mcp::jsonrpc::JsonValue::object();
  (*initializeRequest.params)["clientInfo"] = mcp::jsonrpc::JsonValue::object();
  (*initializeRequest.params)["clientInfo"]["name"] = "cpp-auth-probe";
  (*initializeRequest.params)["clientInfo"]["version"] = "1.0.0";

  mcp::transport::http::ServerRequest probe;
  probe.method = mcp::transport::http::ServerRequestMethod::kPost;
  probe.body = mcp::jsonrpc::serializeMessage(mcp::jsonrpc::Message {initializeRequest});
  mcp::transport::http::setHeader(probe.headers, mcp::transport::http::kHeaderContentType, "application/json");
  mcp::transport::http::setHeader(probe.headers, mcp::transport::http::kHeaderAccept, "application/json, text/event-stream");

  const mcp::transport::http::ServerResponse response = httpRuntime.execute(probe);
  if (response.statusCode != 401)
  {
    throw std::runtime_error("Unauthorized initialize probe expected HTTP 401 but received status " + std::to_string(response.statusCode));
  }

  const std::optional<std::string> challenge = mcp::transport::http::getHeader(response.headers, mcp::transport::http::kHeaderWwwAuthenticate);
  if (!challenge.has_value() || toLowerAscii(*challenge).find("bearer") == std::string::npos)
  {
    throw std::runtime_error("Unauthorized initialize probe did not include a Bearer WWW-Authenticate challenge");
  }
}

}  // namespace

auto main(int argc, char **argv) -> int
{
  try
  {
    const Options options = parseOptions(argc, argv);

    auto client = mcp::Client::create();
    mcp::transport::http::HttpClientOptions clientOptions;
    clientOptions.endpointUrl = options.endpoint;
    if (options.token.has_value())
    {
      clientOptions.bearerToken = options.token;
    }

    client->connectHttp(clientOptions);
    client->start();

    bool observedSamplingRequest = false;
    bool observedElicitationRequest = false;

    mcp::SamplingCapability samplingCapability;
    samplingCapability.tools = true;

    mcp::ElicitationCapability elicitationCapability;
    elicitationCapability.form = true;

    mcp::ClientInitializeConfiguration initializeConfiguration;
    initializeConfiguration.capabilities = mcp::ClientCapabilities(std::nullopt, samplingCapability, elicitationCapability, std::nullopt, std::nullopt);
    client->setInitializeConfiguration(std::move(initializeConfiguration));

    client->setSamplingCreateMessageHandler(
      [&observedSamplingRequest](const mcp::SamplingCreateMessageContext &, const mcp::jsonrpc::JsonValue &params) -> std::optional<mcp::jsonrpc::JsonValue>
      {
        if (!params.is_object() || !params.contains("messages") || !params["messages"].is_array() || params["messages"].empty())
        {
          throw std::runtime_error("sampling/createMessage did not include params.messages");
        }

        const auto &message = params["messages"][0];
        if (!message.is_object() || !message.contains("content") || !message["content"].is_object() || !message["content"].contains("text")
            || !message["content"]["text"].is_string())
        {
          throw std::runtime_error("sampling/createMessage message content was missing text");
        }

        const std::string prompt = message["content"]["text"].as<std::string>();
        if (prompt.find(kExpectedServerSamplingPrompt) == std::string::npos)
        {
          throw std::runtime_error("sampling/createMessage prompt mismatch: " + prompt);
        }

        observedSamplingRequest = true;

        mcp::jsonrpc::JsonValue result = mcp::jsonrpc::JsonValue::object();
        result["role"] = "assistant";
        result["model"] = "cpp-integration-client";
        result["content"] = mcp::jsonrpc::JsonValue::object();
        result["content"]["type"] = "text";
        result["content"]["text"] = std::string(kExpectedClientSamplingResponse);
        result["stopReason"] = "endTurn";
        return result;
      });

    client->setFormElicitationHandler(
      [&observedElicitationRequest](const mcp::ElicitationCreateContext &, const mcp::FormElicitationRequest &request) -> mcp::FormElicitationResult
      {
        if (request.message != kExpectedServerElicitationMessage)
        {
          throw std::runtime_error("elicitation/create message mismatch: " + request.message);
        }

        observedElicitationRequest = true;

        mcp::FormElicitationResult result;
        result.action = mcp::ElicitationAction::kAccept;
        result.content = mcp::jsonrpc::JsonValue::object();
        (*result.content)["approved"] = true;
        (*result.content)["reason"] = std::string(kExpectedClientElicitationReason);
        return result;
      });

    if (options.expectUnauthorized)
    {
      verifyUnauthorizedHttpStatus(options.endpoint);

      try
      {
        const mcp::jsonrpc::Response initializeResponse = client->initialize().get();
        client->stop();
        if (initializeSucceeded(initializeResponse))
        {
          std::cerr << "Expected unauthorized initialize failure, but initialize succeeded" << '\n';
          return 2;
        }

        return 0;
      }
      catch (const std::exception &error)
      {
        client->stop();
        if (!containsAuthorizationSignal(error.what()))
        {
          std::cerr << "Observed unauthorized initialize failure after explicit HTTP 401 probe: " << error.what() << '\n';
          return 0;
        }
        std::cerr << "Observed expected unauthorized failure: " << error.what() << '\n';
        return 0;
      }
    }

    const mcp::jsonrpc::Response initializeResponse = client->initialize().get();
    if (!initializeSucceeded(initializeResponse))
    {
      std::cerr << "initialize did not return success" << '\n';
      client->stop();
      return 3;
    }

    const mcp::ListToolsResult tools = client->listTools();
    if (!hasTool(tools.tools, "python_echo"))
    {
      std::cerr << "python_echo tool was not advertised by reference server" << '\n';
      client->stop();
      return 4;
    }

    mcp::jsonrpc::JsonValue toolArguments = mcp::jsonrpc::JsonValue::object();
    toolArguments["text"] = "from-cpp";
    const mcp::CallToolResult toolResult = client->callTool("python_echo", std::move(toolArguments));
    if (toolResult.isError)
    {
      const std::string errorText = firstTextContent(toolResult);
      std::cerr << "python_echo returned isError=true";
      if (!errorText.empty())
      {
        std::cerr << ": " << errorText;
      }
      std::cerr << '\n';
      client->stop();
      return 5;
    }

    const std::string toolText = firstTextContent(toolResult);
    if (toolText.find("from-cpp") == std::string::npos)
    {
      std::cerr << "python_echo result did not include expected text" << '\n';
      client->stop();
      return 6;
    }

    const mcp::ListResourcesResult resources = client->listResources();
    if (!hasResource(resources.resources, "resource://python-server/info"))
    {
      std::cerr << "resource://python-server/info was not advertised by reference server" << '\n';
      client->stop();
      return 7;
    }

    const mcp::ReadResourceResult resourceResult = client->readResource("resource://python-server/info");
    if (resourceResult.contents.empty() || resourceResult.contents[0].value.find("python reference server") == std::string::npos)
    {
      std::cerr << "Reference resource content was missing expected marker" << '\n';
      client->stop();
      return 8;
    }

    const mcp::ListPromptsResult prompts = client->listPrompts();
    if (!hasPrompt(prompts.prompts, "python_server_prompt"))
    {
      std::cerr << "python_server_prompt was not advertised by reference server" << '\n';
      client->stop();
      return 9;
    }

    mcp::jsonrpc::JsonValue promptArguments = mcp::jsonrpc::JsonValue::object();
    promptArguments["topic"] = "interop";
    const mcp::PromptGetResult promptResult = client->getPrompt("python_server_prompt", std::move(promptArguments));
    if (!promptContainsTopic(promptResult, "interop"))
    {
      std::cerr << "Reference prompt result did not include expected topic" << '\n';
      client->stop();
      return 10;
    }

    if (!observedSamplingRequest)
    {
      std::cerr << "Reference server did not issue sampling/createMessage request to C++ client" << '\n';
      client->stop();
      return 12;
    }

    if (!observedElicitationRequest)
    {
      std::cerr << "Reference server did not issue elicitation/create request to C++ client" << '\n';
      client->stop();
      return 13;
    }

    client->stop();
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "cpp_client_fixture failed: " << error.what() << '\n';
    return 1;
  }
}
